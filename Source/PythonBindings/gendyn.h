#pragma once

#ifdef WASM_TEST
#include "wasm_helpers.h"
#else
#include <cmath>
#include <algorithm>
#include "../Common/xap_utils.h"
#include "sst/basic-blocks/dsp/Interpolators.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
using Xoroshiro128Plus = xenakios::Xoroshiro128Plus;
template <typename T> T clamp(T x, T minx, T maxx)
{
    if (x < minx)
        return minx;
    else if (x > maxx)
        return maxx;
    return x;
}

inline float xcubic_ipol(float y0, float y1, float y2, float y3, float mu)
{
    float a0, a1, a2, a3, mu2;

    mu2 = mu * mu;
    a0 = y3 - y2 - y0 + y1;
    a1 = y0 - y1 - a0;
    a2 = y2 - y0;
    a3 = y1;

    return (a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3);
}

#endif

template <typename T> inline T reflect_value_branchless(const T minval, const T val, const T maxval)
{
    // assert(maxval > minval);

    T range = maxval - minval;
    T double_range = range * 2.0;

    T relative_val = val - minval;

    T wrapped = fmodf(relative_val, double_range);

    if (wrapped < 0)
        wrapped += double_range;

    if (wrapped <= range)
    {
        return minval + wrapped;
    }
    else
    {
        return minval + double_range - wrapped;
    }
}

using pmd_t = sst::basic_blocks::params::ParamMetaData;

struct Gendyn2026
{
    struct Node
    {
        float x0 = 0.0f;
        float y0 = 0.0f;
    };
    static constexpr size_t maxnumnodes = 256;
    alignas(16) Node nodes[maxnumnodes];
    alignas(16) double phase = 0.0;
    alignas(16) double phaseincrement = 0.0;
    std::vector<pmd_t> paramMetaDatas;
    std::unordered_map<uint32_t, pmd_t *> parIdToMetaDataPtr;
    std::array<float, 32> paramValues;
    std::unordered_map<uint32_t, float *> parIdToValuePtr;

    double sr = 0.0;

    enum RANDOMDIST
    {
        RD_HYPCOS,
        RD_CAUCHY
    };
    enum INTERPOLATION
    {
        IP_NONE,
        IP_LINEAR,
        IP_CUBIC
    };

    INTERPOLATION interpmode = IP_LINEAR;

    Xoroshiro128Plus rng;
    alignas(32) float output_buffer[256];
    enum Params
    {
        PAR_RANDSEED,
        PAR_NUMSEGMENTS,
        PAR_INTERPOLATIONMODE,
        PAR_AMPDISTRIBUTION,
        PAR_TIMEDISTRIBUTION,
        PAR_AMPSPREAD,
        PAR_TIMESPREAD,
        PAR_PITCHMIN,
        PAR_PITCHMAX,
        PAR_TRIGRESET
    };

    Gendyn2026()
    {
        paramMetaDatas.push_back(pmd_t()
                                     .asInt()
                                     .withName("Random seed")
                                     .withRange(0.0, 16777216)
                                     .withDefault(42)
                                     .withID(PAR_RANDSEED));
        paramMetaDatas.push_back(
            pmd_t()
                .withName("Interpolation mode")
                .withID(PAR_INTERPOLATIONMODE)
                .withDefault(2)
                .withUnorderedMapFormatting({{0, "No interpolation"}, {1, "Linear"}, {2, "Cubic"}},
                                            true));
        paramMetaDatas.push_back(
            pmd_t()
                .withName("Time distribution")
                .withID(PAR_TIMEDISTRIBUTION)
                .withDefault(0)
                .withUnorderedMapFormatting({{0, "HypCos"}, {1, "Cauchy"}}, true));
        paramMetaDatas.push_back(
            pmd_t()
                .withName("Amplitude distribution")
                .withID(PAR_AMPDISTRIBUTION)
                .withDefault(0)
                .withUnorderedMapFormatting({{0, "HypCos"}, {1, "Cauchy"}}, true));
        paramMetaDatas.push_back(pmd_t()
                                     .asInt()
                                     .withName("Num Segments")
                                     .withRange(4.0, 128.0)
                                     .withDefault(5)
                                     .withID(PAR_NUMSEGMENTS));
        paramMetaDatas.push_back(pmd_t()
                                     .asFloat()
                                     .withName("Amplitude spread")
                                     .withRange(0.0, 1.0)
                                     .withDefault(0.01)
                                     .withID(PAR_AMPSPREAD));
        paramMetaDatas.push_back(pmd_t()
                                     .asFloat()
                                     .withName("Time spread")
                                     .withRange(0.0, 2.0)
                                     .withDefault(0.00)
                                     .withID(PAR_TIMESPREAD));
        paramMetaDatas.push_back(pmd_t()
                                     .asFloat()
                                     .withName("Min pitch")
                                     .withRange(-48.0, 48.0)
                                     .withDefault(0.00)
                                     .withID(PAR_PITCHMIN));
        paramMetaDatas.push_back(pmd_t()
                                     .asFloat()
                                     .withName("Max pitch")
                                     .withRange(-48.0, 48.0)
                                     .withDefault(1.00)
                                     .withID(PAR_PITCHMAX));
        paramMetaDatas.push_back(pmd_t()
                                     .asInt()
                                     .withName("Reset trigger")
                                     .withRange(0.0, 1.0)
                                     .withDefault(0.00)
                                     .withID(PAR_TRIGRESET));
        for (int i = 0; i < paramMetaDatas.size(); ++i)
        {
            paramValues[i] = paramMetaDatas[i].defaultVal;
            parIdToValuePtr[paramMetaDatas[i].id] = &paramValues[i];
            parIdToMetaDataPtr[paramMetaDatas[i].id] = &paramMetaDatas[i];
        }
    }
    float pitchToSamplesTime(float pitchsemis, int numnodes)
    {
        return (sr) / numnodes / (440.0 * std::pow(2.0, 1.0 / 12.0 * (pitchsemis - 9.0)));
    }
    void setInterpolationMode(int m)
    {
        *parIdToValuePtr[PAR_INTERPOLATIONMODE] = m;
        interpmode = (INTERPOLATION)m;
    }
    void prepare(double samplerate)
    {
        sr = samplerate;
        reset();
    }
    void reset()
    {
        float avgpitch = (*parIdToValuePtr[PAR_PITCHMAX]) - (*parIdToValuePtr[PAR_PITCHMIN]) / 2.0;
        float timeavg = pitchToSamplesTime(avgpitch, *parIdToValuePtr[PAR_NUMSEGMENTS]);
        for (size_t i = 0; i < maxnumnodes; ++i)
        {
            nodes[i] = {timeavg, 0.0f};
        }
        phase = 0.0;
        phaseincrement = 1.0 / nodes[0].x0;
    }
    float step()
    {
        float y0 = nodes[0].y0;
        float y1 = nodes[1].y0;
        float y2 = nodes[2].y0;
        float y3 = nodes[3].y0;
        float intery = y0;
        if (interpmode == IP_LINEAR)
            intery = y0 + (y1 - y0) * phase;
        else if (interpmode == IP_CUBIC)
        {
            intery = xcubic_ipol(y0, y1, y2, y3, phase);
            // cubic interpolate will overshoot...
            intery = clamp(intery, -1.0f, 1.0f);
        }
        phase += phaseincrement;
        if (phase >= 1.0)
        {
            int numnodes = *parIdToValuePtr[PAR_NUMSEGMENTS];
// shift/rotate node array left, the current first node will become last node
#ifdef WASM_TEST
            tiny_rotate(&nodes[0], &nodes[1], &nodes[numnodes]);
#else
            std::rotate(&nodes[0], &nodes[1], &nodes[numnodes]);
#endif
            float timespread = *parIdToValuePtr[PAR_TIMESPREAD];
            float x = nodes[numnodes - 1].x0;
            int timedist = *parIdToValuePtr[PAR_TIMEDISTRIBUTION];
            if (timedist == RD_HYPCOS)
                x += rng.nextHypCos(0.0, timespread);
            else if (timedist == RD_CAUCHY)
                x += rng.nextCauchy(0.0, timespread);
            float lowpitch = *parIdToValuePtr[PAR_PITCHMIN];
            float highpitch = *parIdToValuePtr[PAR_PITCHMAX];
            if (lowpitch > highpitch)
                std::swap(lowpitch, highpitch);
            if (lowpitch == highpitch)
                highpitch += 0.1;
            x = reflect_value_branchless(pitchToSamplesTime(highpitch, numnodes), x,
                                         pitchToSamplesTime(lowpitch, numnodes));
            nodes[numnodes - 1].x0 = x;
            float y = nodes[numnodes - 1].y0;
            float ampspread = *parIdToValuePtr[PAR_AMPSPREAD];

            int ampdist = *parIdToValuePtr[PAR_AMPDISTRIBUTION];
            if (ampdist == RD_HYPCOS)
                y += rng.nextHypCos(0.0, ampspread);
            else if (ampdist == RD_CAUCHY)
                y += rng.nextCauchy(0.0, ampspread);
            y = reflect_value_branchless(-1.0f, y, 1.0f);
            nodes[numnodes - 1].y0 = y;
            phaseincrement = 1.0 / nodes[0].x0;
            phase = 0.0;
        }
        return intery * 0.5f;
    }
};
