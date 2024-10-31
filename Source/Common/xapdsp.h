#pragma once

#include <sst/basic-blocks/dsp/FastMath.h>

struct StereoSimperSVF // thanks to urs @ u-he and andy simper @ cytomic
{
    __m128 ic1eq{_mm_setzero_ps()}, ic2eq{_mm_setzero_ps()};
    __m128 g, k, gk, a1, a2, a3, ak;

    __m128 oneSSE{_mm_set1_ps(1.0)};
    __m128 twoSSE{_mm_set1_ps(2.0)};
    enum Mode
    {
        LP,
        HP,
        BP,
        NOTCH,
        PEAK,
        ALL
    };

    void setCoeff(float key, float res, float srInv);

    template <int Mode> static void step(StereoSimperSVF &that, float &L, float &R);
    template <int Mode> static __m128 stepSSE(StereoSimperSVF &that, __m128);

    void init();
};

const float pival = 3.14159265358979323846;

inline void StereoSimperSVF::setCoeff(float key, float res, float srInv)
{
    auto co = 440.0 * pow(2.0, (key - 69.0) / 12);
    co = std::clamp(co, 10.0, 25000.0); // just to be safe/lazy
    res = std::clamp(res, 0.01f, 0.99f);
    g = _mm_set1_ps(sst::basic_blocks::dsp::fasttan(pival * co * srInv));
    k = _mm_set1_ps(2.0 - 2.0 * res);
    gk = _mm_add_ps(g, k);
    a1 = _mm_div_ps(oneSSE, _mm_add_ps(oneSSE, _mm_mul_ps(g, gk)));
    a2 = _mm_mul_ps(g, a1);
    a3 = _mm_mul_ps(g, a2);
    ak = _mm_mul_ps(gk, a1);
}

template <int FilterMode>
inline void StereoSimperSVF::step(StereoSimperSVF &that, float &L, float &R)
{
    auto vin = _mm_set_ps(0, 0, R, L);
    auto res = stepSSE<FilterMode>(that, vin);
    float r4 alignas(16)[4];
    _mm_store_ps(r4, res);
    L = r4[0];
    R = r4[1];
}

template <int FilterMode> inline __m128 StereoSimperSVF::stepSSE(StereoSimperSVF &that, __m128 vin)
{
    // auto v3 = vin[c] - ic2eq[c];
    auto v3 = _mm_sub_ps(vin, that.ic2eq);
    // auto v0 = a1 * v3 - ak * ic1eq[c];
    auto v0 = _mm_sub_ps(_mm_mul_ps(that.a1, v3), _mm_mul_ps(that.ak, that.ic1eq));
    // auto v1 = a2 * v3 + a1 * ic1eq[c];
    auto v1 = _mm_add_ps(_mm_mul_ps(that.a2, v3), _mm_mul_ps(that.a1, that.ic1eq));

    // auto v2 = a3 * v3 + a2 * ic1eq[c] + ic2eq[c];
    auto v2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(that.a3, v3), _mm_mul_ps(that.a2, that.ic1eq)),
                         that.ic2eq);

    // ic1eq[c] = 2 * v1 - ic1eq[c];
    that.ic1eq = _mm_sub_ps(_mm_mul_ps(that.twoSSE, v1), that.ic1eq);
    // ic2eq[c] = 2 * v2 - ic2eq[c];
    that.ic2eq = _mm_sub_ps(_mm_mul_ps(that.twoSSE, v2), that.ic2eq);

    __m128 res;

    switch (FilterMode)
    {
    case LP:
        res = v2;
        break;
    case BP:
        res = v1;
        break;
    case HP:
        res = v0;
        break;
    case NOTCH:
        res = _mm_add_ps(v2, v0);
        break;
    case PEAK:
        res = _mm_sub_ps(v2, v0);
        break;
    case ALL:
        res = _mm_sub_ps(_mm_add_ps(v2, v0), _mm_mul_ps(that.k, v1));
        break;
    default:
        res = _mm_setzero_ps();
    }

    return res;
}

inline void StereoSimperSVF::init()
{
    ic1eq = _mm_setzero_ps();
    ic2eq = _mm_setzero_ps();
}
