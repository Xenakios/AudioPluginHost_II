#pragma once

#ifdef WASM_TEST
#include "wasm_helpers.h"
#else
#include <cmath>
#include <algorithm>
#include "../Common/xap_utils.h"
#include "sst/basic-blocks/dsp/Interpolators.h"
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
    size_t numnodes = 5;
    double sr = 0.0;
    float ampspread = 0.01f;
    float timespread = 0.0f;
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
    RANDOMDIST timedist = RD_HYPCOS;
    RANDOMDIST ampdist = RD_HYPCOS;
    INTERPOLATION interpmode = IP_LINEAR;
    float timelowvalue = 1.0;
    float timehighvalue = 256.0;
    Xoroshiro128Plus rng;
    alignas(32) float output_buffer[256];
    Gendyn2026()
    {
        float timeavg = timelowvalue + (timehighvalue - timelowvalue) / 2;
        for (size_t i = 0; i < maxnumnodes; ++i)
        {
            nodes[i] = {timeavg, 0.0f};
        }
    }
    void prepare(double samplerate)
    {
        sr = samplerate;
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
// shift/rotate node array left, the current first node will become last node
#ifdef WASM_TEST
            tiny_rotate(&nodes[0], &nodes[1], &nodes[numnodes]);
#else
            std::rotate(&nodes[0], &nodes[1], &nodes[numnodes]);
#endif
            float x = nodes[numnodes - 1].x0;
            if (timedist == RD_HYPCOS)
                x += rng.nextHypCos(0.0, timespread);
            else if (timedist == RD_CAUCHY)
                x += rng.nextCauchy(0.0, timespread);
            x = reflect_value_branchless(timelowvalue, x, timehighvalue);
            nodes[numnodes - 1].x0 = x;
            float y = nodes[numnodes - 1].y0;
            if (ampdist == RD_HYPCOS)
                y += rng.nextHypCos(0.0, ampspread);
            else if (ampdist == RD_CAUCHY)
                y += rng.nextCauchy(0.0, ampspread);
            y = reflect_value_branchless(-0.5f, y, 0.5f);
            nodes[numnodes - 1].y0 = y;
            phaseincrement = 1.0 / nodes[0].x0;
            phase = 0.0;
        }
        return intery;
    }
};
