#pragma once

#include "sst/filters++.h"
#include "text/choc_StringUtilities.h"
#include "airwin_consolidated_base.h"
#include <print>

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
    std::array<float, 10> paramvalues;
    alignas(32) sst::filtersplusplus::Filter sstfilter;
    alignas(32) std::unique_ptr<AirwinConsolidatedBase> awplugin;
    size_t mainmode = 0;
    double sr = 0.0;
    size_t blockSize = 0;
    size_t numParams = 0;
    std::string getParameterName(size_t index)
    {
        if (mainmode == 0)
            return "No parameter";
        if (mainmode == 1)
        {
            if (index == 0)
                return "Cutoff";
            else if (index == 1)
                return "Resonance";
            else if (index == 2)
                return "Extra parameter";
            else if (index == 3)
                return "Cutoff spread";
            return "No parameter";
        }
        if (mainmode == 2 && awplugin && index < numParams)
        {
            char buf[256];
            memset(buf, 0, 256);
            awplugin->getParameterName(index, buf);
            return buf;
        }
        return "No parameter";
    }
    std::vector<float> delaylinememory;
    GrainInsertFX()
    {
        std::fill(paramvalues.begin(), paramvalues.end(), 0.0f);
        delaylinememory.resize(8192);
    }

    static std::vector<ModeInfo> getAvailableModes();

    void setMode(ModeInfo m);

    void reset()
    {
        if (mainmode == 1)
            sstfilter.reset();
        if (mainmode == 2 && awplugin)
            awplugin->reset();
    }
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
            // we might be clamped properly before this but adding this for safety now
            v = std::clamp(v, 0.0f, 1.0f);
            awplugin->setParameter(index, v);
        }
    }
    void prepareBlock()
    {
        if (mainmode == 1)
        {
            sstfilter.makeCoefficients(0, paramvalues[0] - paramvalues[3], paramvalues[1],
                                       paramvalues[2]);
            sstfilter.makeCoefficients(1, paramvalues[0] + paramvalues[3], paramvalues[1],
                                       paramvalues[2]);
            sstfilter.prepareBlock();
        }
        else if (mainmode == 2)
        {
            assert(awplugin);
            // airwindows plugins probably just do a switch case and assign of a
            // variable per setParameter, but we might want to avoid setting all the
            // parameters if not really needed...
            for (size_t i = 0; i < numParams; ++i)
                awplugin->setParameter(i, std::clamp(paramvalues[i], 0.0f, 1.0f));
        }
    }
    void processStereo(float &inleft, float &inright)
    {
        switch (mainmode)
        {
        case 0:
            break;
        case 1:
        {
            float outLeft = 0.0f;
            float outRight = 0.0f;
            sstfilter.processStereoSample(inleft, inright, outLeft, outRight);
            inleft = outLeft;
            inright = outRight;
            break;
        }
        case 2:
        {
            assert(awplugin);
            float input0 = inleft;
            float input1 = inright;
            float *inputs[2] = {&input0, &input1};
            float output0 = 0.0f;
            float output1 = 0.0f;
            float *outputs[2] = {&output0, &output1};
            awplugin->processReplacing(inputs, outputs, 1);
            inleft = output0;
            inright = output1;
        }
        }
    };
    void concludeBlock()
    {
        if (mainmode == 1)
            sstfilter.concludeBlock();
    }
};
