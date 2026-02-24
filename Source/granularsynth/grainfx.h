#pragma once

#include "sst/filters++.h"
#include "text/choc_StringUtilities.h"
#include "airwin_consolidated_base.h"
#include "plugins/BezEQ.h"
#include "plugins/HipCrush.h"

namespace sfpp = sst::filtersplusplus;
struct FilterInfo
{
    std::string address;
    sfpp::FilterModel model;
    sfpp::FilterSubModel submodel;
    sfpp::ModelConfig modelconfig;
};

inline std::vector<FilterInfo> g_filter_infos;

static void init_filter_infos()
{
    if (g_filter_infos.size() > 0)
        return;
    g_filter_infos.reserve(256);
    auto models = sfpp::Filter::availableModels();
    std::string address;
    address.reserve(256);
    FilterInfo ninfo;
    ninfo.address = "none";
    ninfo.model = sst::filtersplusplus::FilterModel::None;
    ninfo.modelconfig = {};
    g_filter_infos.push_back(ninfo);
    for (auto &mod : models)
    {
        auto subm = sfpp::Filter::availableModelConfigurations(mod, true);
        for (auto s : subm)
        {
            address = sfpp::toString(mod);
            if (s == sfpp::ModelConfig())
            {
            }
            auto [pt, st, dt, smt] = s;
            if (pt != sfpp::Passband::UNSUPPORTED)
            {
                address += "/" + sfpp::toString(pt);
            }
            if (st != sfpp::Slope::UNSUPPORTED)
            {
                address += "/" + sfpp::toString(st);
            }
            if (dt != sfpp::DriveMode::UNSUPPORTED)
            {
                address += "/" + sfpp::toString(dt);
            }
            if (smt != sfpp::FilterSubModel::UNSUPPORTED)
            {
                address += "/" + sfpp::toString(smt);
            }
            address = choc::text::toLowerCase(address);
            address = choc::text::replace(address, " ", "_", "&", "and", ",", "");
            FilterInfo info;
            info.address = address;
            info.model = mod;
            info.modelconfig = s;
            g_filter_infos.push_back(info);
        }
    }
}

// Chris has sometimes forgot to initialize variables, so with this we will get at least
// zeros for those in the hopes that avoids problems
template <typename AWType>
[[nodiscard]] inline std::unique_ptr<AirwinConsolidatedBase> make_aw_safe(audioMasterCallback cb)
{
    static_assert(std::derived_from<AWType, AirwinConsolidatedBase>,
                  "class must inherit from AirwinConsolidatedBase");
    char *objbuffer = new char[sizeof(AWType)];
    std::fill(objbuffer, objbuffer + sizeof(AWType), 0);
    AirwinConsolidatedBase *ob = new (objbuffer) AWType(cb);
    return std::unique_ptr<AirwinConsolidatedBase>(ob);
}

class GrainInsertFX
{
  public:
    struct ModeInfo
    {
        std::string displayname;
        std::string groupname;
        std::string factoryname;
    };
    std::array<float, 12> paramvalues;
    alignas(32) sst::filtersplusplus::Filter sstfilter;
    alignas(32) std::unique_ptr<AirwinConsolidatedBase> awplugin;
    size_t mainmode = 0;
    double sr = 0.0;
    size_t blockSize = 0;
    size_t numParams = 0;
    std::vector<float> delaylinememory;
    GrainInsertFX()
    {
        std::fill(paramvalues.begin(), paramvalues.end(), 0.0f);
        delaylinememory.resize(4096);
    }

    std::vector<ModeInfo> getAvailableModes()
    {
        std::vector<ModeInfo> result;
        result.emplace_back("None", "", "none");
        for (auto &gi : g_filter_infos)
        {
            result.emplace_back(gi.address, "SST Filters", "sstf/" + gi.address);
        }
        result.emplace_back("BezEQ", "AirWindows", "aw/bezeq");
        result.emplace_back("HipCrush", "AirWindows", "aw/hipcrush");
        return result;
    }
    void setMode(ModeInfo m)
    {
        if (m.groupname.empty())
        {
            std::fill(paramvalues.begin(), paramvalues.end(), 0.0f);
            mainmode = 0;
            numParams = 0;
        }
        if (m.groupname == "SST Filters")
        {
            mainmode = 1;
            numParams = 3;
            paramvalues[0] = 72.0;
            paramvalues[1] = 0.0;
            paramvalues[2] = 0.0;
            sst::filtersplusplus::FilterModel model{sst::filtersplusplus::FilterModel::CytomicSVF};
            sst::filtersplusplus::ModelConfig conf;
            conf.pt = sst::filtersplusplus::Passband::LP;
            auto reqdelaysize = sst::filtersplusplus::Filter::requiredDelayLinesSizes(model, conf);
            // std::print("filter {} requires {} samples of delay line\n", finfo.address,
            // reqdelaysize);
            if (reqdelaysize > delaylinememory.size())
                delaylinememory.resize(reqdelaysize);
            sstfilter.setFilterModel(model);
            sstfilter.setModelConfiguration(conf);
            sstfilter.setSampleRateAndBlockSize(sr, blockSize);
            sstfilter.setMono();
            sstfilter.provideDelayLine(0, delaylinememory.data());
            if (!sstfilter.prepareInstance())
            {
                std::print("could not prepare filter\n");
            }
        }
        if (m.groupname == "AirWindows")
        {
            mainmode = 0;
            if (m.factoryname == "aw/bezeq")
            {
                awplugin = make_aw_safe<airwinconsolidated::BezEQ::BezEQ>(0);
                numParams = airwinconsolidated::BezEQ::kNumParameters;
            }
            else if (m.factoryname == "aw/hipcrush")
            {
                awplugin = make_aw_safe<airwinconsolidated::HipCrush::HipCrush>(0);
                numParams = airwinconsolidated::HipCrush::kNumParameters;
            }
            if (awplugin)
            {
                mainmode = 2;
                awplugin->setNumInputs(1);
                awplugin->setNumOutputs(1);
                awplugin->setSampleRate(sr);
                for (size_t i = 0; i < numParams; ++i)
                {
                    paramvalues[i] = awplugin->getParameter(i);
                }
            }
        }
    }
    void prepareInstance(double sampleRate, size_t ablockSize)
    {
        sr = sampleRate;
        blockSize = ablockSize;
    }
    void setParameter(uint32_t index, float v)
    {
        if (mainmode == 1)
        {
            paramvalues[index] = v;
            sstfilter.makeCoefficients(0, paramvalues[0], paramvalues[1], paramvalues[2]);
        }
        else if (mainmode == 2)
        {
            paramvalues[index] = v;
        }
    }
    void prepareBlock()
    {
        if (mainmode == 1)
            sstfilter.prepareBlock();
    }
    float processMonoSample(float insample)
    {
        switch (mainmode)
        {
        case 0:
            return insample;
        case 1:
            return sstfilter.processMonoSample(insample);
        case 2:
        {
            assert(awplugin);
            float *inputs[1] = {&insample};
            float output = 0.0f;
            float *outputs[1] = {&output};
            awplugin->processReplacing(inputs, outputs, 1);
            return output;
        }
        }
        return 0.0f;
    };
    void concludeBlock()
    {
        if (mainmode == 1)
            sstfilter.concludeBlock();
    }
};
