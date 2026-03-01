#pragma once
#include <cmath>
// taken from the IEM plugins code
// https://plugins.iem.at/

const float n3d2sn3d[64] = {
    1.0000000000000000e+00f, 5.7735026918962584e-01f, 5.7735026918962584e-01f,
    5.7735026918962584e-01f, 4.4721359549995793e-01f, 4.4721359549995793e-01f,
    4.4721359549995793e-01f, 4.4721359549995793e-01f, 4.4721359549995793e-01f,
    3.7796447300922720e-01f, 3.7796447300922720e-01f, 3.7796447300922720e-01f,
    3.7796447300922720e-01f, 3.7796447300922720e-01f, 3.7796447300922720e-01f,
    3.7796447300922720e-01f, 3.3333333333333331e-01f, 3.3333333333333331e-01f,
    3.3333333333333331e-01f, 3.3333333333333331e-01f, 3.3333333333333331e-01f,
    3.3333333333333331e-01f, 3.3333333333333331e-01f, 3.3333333333333331e-01f,
    3.3333333333333331e-01f, 3.0151134457776363e-01f, 3.0151134457776363e-01f,
    3.0151134457776363e-01f, 3.0151134457776363e-01f, 3.0151134457776363e-01f,
    3.0151134457776363e-01f, 3.0151134457776363e-01f, 3.0151134457776363e-01f,
    3.0151134457776363e-01f, 3.0151134457776363e-01f, 3.0151134457776363e-01f,
    2.7735009811261457e-01f, 2.7735009811261457e-01f, 2.7735009811261457e-01f,
    2.7735009811261457e-01f, 2.7735009811261457e-01f, 2.7735009811261457e-01f,
    2.7735009811261457e-01f, 2.7735009811261457e-01f, 2.7735009811261457e-01f,
    2.7735009811261457e-01f, 2.7735009811261457e-01f, 2.7735009811261457e-01f,
    2.7735009811261457e-01f, 2.5819888974716110e-01f, 2.5819888974716110e-01f,
    2.5819888974716110e-01f, 2.5819888974716110e-01f, 2.5819888974716110e-01f,
    2.5819888974716110e-01f, 2.5819888974716110e-01f, 2.5819888974716110e-01f,
    2.5819888974716110e-01f, 2.5819888974716110e-01f, 2.5819888974716110e-01f,
    2.5819888974716110e-01f, 2.5819888974716110e-01f, 2.5819888974716110e-01f,
    2.5819888974716110e-01f,
};

template <typename Type>
static void sphericalToCartesian(const Type azimuthInRadians, const Type elevationInRadians,
                                 Type &x, Type &y, Type &z)
{
    const Type cosElevation = cos(elevationInRadians);
    x = cosElevation * cos(azimuthInRadians);
    y = cosElevation * sin(azimuthInRadians);
    z = sin(elevationInRadians);
}

// order 1
inline void SHEval1(const float fX, const float fY, const float fZ, float *pSH)
{
    float fC0, fS0, fTmpC;

    pSH[0] = 0.2820947917738781f;
    pSH[2] = 0.4886025119029199f * fZ;
    fC0 = fX;
    fS0 = fY;

    fTmpC = 0.48860251190292f;
    pSH[3] = fTmpC * fC0;
    pSH[1] = fTmpC * fS0;
}

// order 2
inline void SHEval2(const float fX, const float fY, const float fZ, float *pSH)
{
    float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
    float fZ2 = fZ * fZ;

    pSH[0] = 0.2820947917738781f;
    pSH[2] = 0.4886025119029199f * fZ;
    pSH[6] = 0.9461746957575601f * fZ2 + -0.31539156525252f;
    fC0 = fX;
    fS0 = fY;

    fTmpA = 0.48860251190292f;
    pSH[3] = fTmpA * fC0;
    pSH[1] = fTmpA * fS0;
    fTmpB = 1.092548430592079f * fZ;
    pSH[7] = fTmpB * fC0;
    pSH[5] = fTmpB * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpC = 0.5462742152960395f;
    pSH[8] = fTmpC * fC1;
    pSH[4] = fTmpC * fS1;
}

// order 3
inline void SHEval3(const float fX, const float fY, const float fZ, float *pSH)
{
    float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
    float fZ2 = fZ * fZ;

    pSH[0] = 0.2820947917738781f;
    pSH[2] = 0.4886025119029199f * fZ;
    pSH[6] = 0.9461746957575601f * fZ2 + -0.31539156525252f;
    pSH[12] = fZ * (1.865881662950577f * fZ2 + -1.119528997770346f);
    fC0 = fX;
    fS0 = fY;

    fTmpA = 0.48860251190292f;
    pSH[3] = fTmpA * fC0;
    pSH[1] = fTmpA * fS0;
    fTmpB = 1.092548430592079f * fZ;
    pSH[7] = fTmpB * fC0;
    pSH[5] = fTmpB * fS0;
    fTmpC = 2.285228997322329f * fZ2 + -0.4570457994644658f;
    pSH[13] = fTmpC * fC0;
    pSH[11] = fTmpC * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpA = 0.5462742152960395f;
    pSH[8] = fTmpA * fC1;
    pSH[4] = fTmpA * fS1;
    fTmpB = 1.445305721320277f * fZ;
    pSH[14] = fTmpB * fC1;
    pSH[10] = fTmpB * fS1;
    fC0 = fX * fC1 - fY * fS1;
    fS0 = fX * fS1 + fY * fC1;

    fTmpC = 0.5900435899266435f;
    pSH[15] = fTmpC * fC0;
    pSH[9] = fTmpC * fS0;
}

// This assumes 'coeffs' is the output from IEM's SHEval2 (9 floats)
inline void apply_focus_weights(float *coeffs, float morph)
{
    // Weights for 2nd order Max-rE (focused beam)
    // At morph = 0.0: Omni
    // At morph = 0.5: 1st Order
    // At morph = 1.0: 2nd Order Max-rE (Optimal focus)

    float w1, w2;

    if (morph < 0.5f)
    {
        float t = morph * 2.0f;
        w1 = t; // Fade in 1st order
        w2 = 0.0f;
    }
    else
    {
        float t = (morph - 0.5f) * 2.0f;
        // Interpolate between full 1st order and 2nd order Max-rE
        w1 = 1.0f + t * (0.775f - 1.0f);
        w2 = t * 0.400f;
    }

    // Apply weights to ACN channels
    // 0 = W (stays 1.0)
    for (int i = 1; i < 4; i++)
        coeffs[i] *= w1; // Order 1
    for (int i = 4; i < 9; i++)
        coeffs[i] *= w2; // Order 2
}

inline void focus_coeffs3rd_order(float *coeffs, float morph)
{
    float w1 = 0.0f, w2 = 0.0f, w3 = 0.0f;

    if (morph < 0.333f)
    {
        // Phase 1: Omni to 1st Order (Max-rE)
        float t = morph * 3.0f;
        w1 = t * 0.577f;
    }
    else if (morph < 0.666f)
    {
        // Phase 2: 1st Order to 2nd Order (Max-rE)
        float t = (morph - 0.333f) * 3.0f;
        w1 = 0.577f + t * (0.775f - 0.577f);
        w2 = t * 0.400f;
    }
    else
    {
        // Phase 3: 2nd Order to 3rd Order (Max-rE)
        float t = (morph - 0.666f) * 3.0f;
        w1 = 0.775f + t * (0.861f - 0.775f);
        w2 = 0.400f + t * (0.612f - 0.400f);
        w3 = t * 0.323f;
    }

    // Apply weights (Assuming ACN ordering)
    // Order 0: index 0 (Weight is always 1.0)
    for (int i = 1; i < 4; i++)
        coeffs[i] *= w1; // Order 1
    for (int i = 4; i < 9; i++)
        coeffs[i] *= w2; // Order 2
    for (int i = 9; i < 16; i++)
        coeffs[i] *= w3; // Order 3
}

inline void focus_coeffs2nd_order(float *coeffs, float *dest, float morph)
{
    const float table[4][10] = {{1, 0, 0, 0, 0, 0, 0, 0, 0},
                                {1, 1, 1, 1, 0, 0, 0, 0, 0},
                                {1, 1, 1, 1, 1, 1, 1, 1, 1},
                                {1, 1, 1, 1, 1, 1, 1, 1, 1}};
    morph *= 2.0;
    int i0 = morph;
    int i1 = i0 + 1;
    float frac = morph - (int)morph;
    for (int i = 0; i < 9; ++i)
    {
        float y0 = table[i0][i];
        float y1 = table[i1][i];
        float y2 = y0 + (y1 - y0) * frac;
        dest[i] = coeffs[i] * y2;
    }
}

template <size_t MaxDelay> struct AllPass
{
    float delayBuffer[MaxDelay + 1] = {0.0f};
    float writeIndex = 0; // Still write at integer steps
    int bufferSize = MaxDelay;
    float g = 0.5f;

    // Use a float for delayLength to allow modulation
    float process(float input, float currentDelay)
    {
        // 1. Calculate read position
        float readPos = (float)writeIndex - currentDelay;
        while (readPos < 0)
            readPos += bufferSize;

        // 2. Linear Interpolation
        int i0 = (int)readPos;
        int i1 = (i0 + 1) % bufferSize;
        float frac = readPos - i0;

        float delayedSample = delayBuffer[i0] + frac * (delayBuffer[i1] - delayBuffer[i0]);

        // 3. All-pass logic (Standard Form)
        float vn = input + (g * delayedSample);
        float output = delayedSample - (g * vn);

        // 4. Update buffer and index
        delayBuffer[(int)writeIndex] = vn;
        writeIndex = (int)(writeIndex + 1) % bufferSize;

        return output;
    }
};

template <size_t NumFilters> struct AllPassBank
{
    AllPass<4096> filters[NumFilters];
    AllPassBank()
    {
        for (size_t i = 0; i < NumFilters; ++i)
        {
            phases[i] = 0.0;
            lforates[i] = 0.4 + 0.23 / NumFilters * i;
            lfoamounts[i] = 5.0;
        }
    }
    float samplerate = 0.0;
    float basedelaytimes[NumFilters];
    float lforates[NumFilters];
    float lfoamounts[NumFilters];
    double phases[NumFilters];
    float process(size_t filterIndex, float input, float mix)
    {
        float moddeltime =
            basedelaytimes[filterIndex] + lfoamounts[filterIndex] * std::sin(phases[filterIndex]);
        float dry = input;
        float wet = filters[filterIndex].process(dry, moddeltime);
        float output = (1.0f - mix) * dry + mix * wet;
        phases[filterIndex] += (M_PI * 2 * lforates[filterIndex]) / samplerate;
        return output;
    }
};
