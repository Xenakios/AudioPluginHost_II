#pragma once

#include "sst/filters++.h"
#include "text/choc_StringUtilities.h"
#include "airwin_consolidated_base.h"
#include "plugins/BezEQ.h"
#include "plugins/HipCrush.h"
#include "plugins/kWoodRoom.h"
#include "plugins/RingModulator.h"

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
        uint8_t mainmode = 0;
        uint8_t awtype = 0;
        sst::filtersplusplus::FilterModel sstmodel;
        sst::filtersplusplus::ModelConfig sstconfig;
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

    static std::vector<ModeInfo> getAvailableModes()
    {
        std::vector<ModeInfo> result;
        result.emplace_back("None", "");
        result.emplace_back("AW BezEQ", "AirWindows", 2, 0);
        result.emplace_back("AW HipCrush", "AirWindows", 2, 1);
        result.emplace_back("AW KWoodRoom", "AirWindows", 2, 2);
        result.emplace_back("AW RingModulator", "AirWindows", 2, 3);
        auto models = sfpp::Filter::availableModels();
        for (auto &mo : models)
        {
            auto confs = sfpp::Filter::availableModelConfigurations(mo);
            for (auto co : confs)
            {
                auto [pt, st, dt, smt] = co;
                std::string subname;
                if (pt != sfpp::Passband::UNSUPPORTED)
                {
                    subname += " " + sfpp::toString(pt);
                }
                if (st != sfpp::Slope::UNSUPPORTED)
                {
                    subname += " " + sfpp::toString(st);
                }
                if (dt != sfpp::DriveMode::UNSUPPORTED)
                {
                    subname += " " + sfpp::toString(dt);
                }
                if (smt != sfpp::FilterSubModel::UNSUPPORTED)
                {
                    subname += " " + sfpp::toString(smt);
                }
                result.emplace_back(sfpp::toString(mo) + " " + subname, sfpp::toString(mo), 1, 0,
                                    mo, co);
            }
        }

        return result;
    }
    void setMode(ModeInfo m)
    {
        assert(sr > 0);
        if (m.mainmode == 0)
        {
            std::fill(paramvalues.begin(), paramvalues.end(), 0.0f);
            mainmode = 0;
            numParams = 0;
        }
        if (m.mainmode == 1)
        {
            mainmode = 1;
            numParams = 3;
            paramvalues[0] = 1.0;
            paramvalues[1] = 0.0;
            paramvalues[2] = 0.0;
            auto reqdelaysize =
                sst::filtersplusplus::Filter::requiredDelayLinesSizes(m.sstmodel, m.sstconfig);
            // std::print("filter {} requires {} samples of delay line\n", finfo.address,
            // reqdelaysize);
            if (reqdelaysize > delaylinememory.size())
                delaylinememory.resize(reqdelaysize);
            sstfilter.setFilterModel(m.sstmodel);
            sstfilter.setModelConfiguration(m.sstconfig);
            sstfilter.setSampleRateAndBlockSize(sr, blockSize);
            sstfilter.setMono();
            sstfilter.provideDelayLine(0, delaylinememory.data());
            if (!sstfilter.prepareInstance())
            {
                std::print("could not prepare filter {}\n", m.displayname);
            }
        }
        if (m.mainmode == 2)
        {
            mainmode = 0;
            numParams = 0;
            if (m.awtype == 0)
            {
                awplugin = make_aw_safe<airwinconsolidated::BezEQ::BezEQ>(0);
                numParams = airwinconsolidated::BezEQ::kNumParameters;
            }
            else if (m.awtype == 1)
            {
                awplugin = make_aw_safe<airwinconsolidated::HipCrush::HipCrush>(0);
                numParams = airwinconsolidated::HipCrush::kNumParameters;
            }
            else if (m.awtype == 2)
            {
                awplugin = make_aw_safe<airwinconsolidated::kWoodRoom::kWoodRoom>(0);
                numParams = airwinconsolidated::kWoodRoom::kNumParameters;
            }
            else if (m.awtype == 3)
            {
                awplugin = make_aw_safe<airwinconsolidated::RingModulator::RingModulator>(0);
                numParams = airwinconsolidated::RingModulator::kNumParameters;
            }
            if (awplugin)
            {
                mainmode = 2;
                awplugin->setNumInputs(2);
                awplugin->setNumOutputs(2);
                awplugin->setSampleRate(sr);
                std::fill(paramvalues.begin(), paramvalues.end(), 0.0f);
                for (size_t i = 0; i < numParams; ++i)
                {
                    paramvalues[i] = awplugin->getParameter(i);
                }
            }
        }
    }
    void reset() { sstfilter.reset(); }
    void prepareInstance(double sampleRate, size_t ablockSize)
    {
        sr = sampleRate;
        blockSize = ablockSize;
    }
    void setParameter(uint32_t index, float v)
    {
        assert(index < numParams);
        paramvalues[index] = v;
        if (mainmode == 1)
        {
            sstfilter.makeCoefficients(0, paramvalues[0], paramvalues[1], paramvalues[2]);
        }
        else if (mainmode == 2)
        {
            assert(awplugin);
            awplugin->setParameter(index, v);
        }
    }
    void prepareBlock()
    {
        if (mainmode == 1)
        {
            sstfilter.makeCoefficients(0, paramvalues[0], paramvalues[1], paramvalues[2]);
            sstfilter.prepareBlock();
        }
        else if (mainmode == 2)
        {
            assert(awplugin);
            // airwindows plugins probably just do a switch case and assign of a
            // variable per setParameter, but we might want to avoid setting all the
            // parameters if not really needed...
            for (size_t i = 0; i < numParams; ++i)
                awplugin->setParameter(i, paramvalues[i]);
        }
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
            float input0 = insample;
            float input1 = insample;
            float *inputs[2] = {&input0, &input1};
            float output0 = 0.0f;
            float output1 = 0.0f;
            float *outputs[2] = {&output0, &output1};
            awplugin->processReplacing(inputs, outputs, 1);
            return (output0 + output1) * 0.5f;
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
