#pragma once
#include <cmath>
#include "sst/basic-blocks/dsp/SmoothingStrategies.h"

template <typename T> inline T degreesToRadians(T degrees) { return degrees * (M_PI / 180.0); }

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

// order 4
inline void SHEval4(const float fX, const float fY, const float fZ, float *pSH)
{
    float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
    float fZ2 = fZ * fZ;

    pSH[0] = 0.2820947917738781f;
    pSH[2] = 0.4886025119029199f * fZ;
    pSH[6] = 0.9461746957575601f * fZ2 + -0.31539156525252f;
    pSH[12] = fZ * (1.865881662950577f * fZ2 + -1.119528997770346f);
    pSH[20] = 1.984313483298443f * fZ * pSH[12] + -1.006230589874905f * pSH[6];
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
    fTmpA = fZ * (4.683325804901025f * fZ2 + -2.007139630671868f);
    pSH[21] = fTmpA * fC0;
    pSH[19] = fTmpA * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpA = 0.5462742152960395f;
    pSH[8] = fTmpA * fC1;
    pSH[4] = fTmpA * fS1;
    fTmpB = 1.445305721320277f * fZ;
    pSH[14] = fTmpB * fC1;
    pSH[10] = fTmpB * fS1;
    fTmpC = 3.31161143515146f * fZ2 + -0.47308734787878f;
    pSH[22] = fTmpC * fC1;
    pSH[18] = fTmpC * fS1;
    fC0 = fX * fC1 - fY * fS1;
    fS0 = fX * fS1 + fY * fC1;

    fTmpA = 0.5900435899266435f;
    pSH[15] = fTmpA * fC0;
    pSH[9] = fTmpA * fS0;
    fTmpB = 1.770130769779931f * fZ;
    pSH[23] = fTmpB * fC0;
    pSH[17] = fTmpB * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpC = 0.6258357354491763f;
    pSH[24] = fTmpC * fC1;
    pSH[16] = fTmpC * fS1;
}

// order 5
inline void SHEval5(const float fX, const float fY, const float fZ, float *pSH)
{
    float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
    float fZ2 = fZ * fZ;

    pSH[0] = 0.2820947917738781f;
    pSH[2] = 0.4886025119029199f * fZ;
    pSH[6] = 0.9461746957575601f * fZ2 + -0.31539156525252f;
    pSH[12] = fZ * (1.865881662950577f * fZ2 + -1.119528997770346f);
    pSH[20] = 1.984313483298443f * fZ * pSH[12] + -1.006230589874905f * pSH[6];
    pSH[30] = 1.98997487421324f * fZ * pSH[20] + -1.002853072844814f * pSH[12];
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
    fTmpA = fZ * (4.683325804901025f * fZ2 + -2.007139630671868f);
    pSH[21] = fTmpA * fC0;
    pSH[19] = fTmpA * fS0;
    fTmpB = 2.03100960115899f * fZ * fTmpA + -0.991031208965115f * fTmpC;
    pSH[31] = fTmpB * fC0;
    pSH[29] = fTmpB * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpA = 0.5462742152960395f;
    pSH[8] = fTmpA * fC1;
    pSH[4] = fTmpA * fS1;
    fTmpB = 1.445305721320277f * fZ;
    pSH[14] = fTmpB * fC1;
    pSH[10] = fTmpB * fS1;
    fTmpC = 3.31161143515146f * fZ2 + -0.47308734787878f;
    pSH[22] = fTmpC * fC1;
    pSH[18] = fTmpC * fS1;
    fTmpA = fZ * (7.190305177459987f * fZ2 + -2.396768392486662f);
    pSH[32] = fTmpA * fC1;
    pSH[28] = fTmpA * fS1;
    fC0 = fX * fC1 - fY * fS1;
    fS0 = fX * fS1 + fY * fC1;

    fTmpA = 0.5900435899266435f;
    pSH[15] = fTmpA * fC0;
    pSH[9] = fTmpA * fS0;
    fTmpB = 1.770130769779931f * fZ;
    pSH[23] = fTmpB * fC0;
    pSH[17] = fTmpB * fS0;
    fTmpC = 4.403144694917254f * fZ2 + -0.4892382994352505f;
    pSH[33] = fTmpC * fC0;
    pSH[27] = fTmpC * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpA = 0.6258357354491763f;
    pSH[24] = fTmpA * fC1;
    pSH[16] = fTmpA * fS1;
    fTmpB = 2.075662314881041f * fZ;
    pSH[34] = fTmpB * fC1;
    pSH[26] = fTmpB * fS1;
    fC0 = fX * fC1 - fY * fS1;
    fS0 = fX * fS1 + fY * fC1;

    fTmpC = 0.6563820568401703f;
    pSH[35] = fTmpC * fC0;
    pSH[25] = fTmpC * fS0;
}

// order 6
inline void SHEval6(const float fX, const float fY, const float fZ, float *pSH)
{
    float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
    float fZ2 = fZ * fZ;

    pSH[0] = 0.2820947917738781f;
    pSH[2] = 0.4886025119029199f * fZ;
    pSH[6] = 0.9461746957575601f * fZ2 + -0.31539156525252f;
    pSH[12] = fZ * (1.865881662950577f * fZ2 + -1.119528997770346f);
    pSH[20] = 1.984313483298443f * fZ * pSH[12] + -1.006230589874905f * pSH[6];
    pSH[30] = 1.98997487421324f * fZ * pSH[20] + -1.002853072844814f * pSH[12];
    pSH[42] = 1.993043457183566f * fZ * pSH[30] + -1.001542020962219f * pSH[20];
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
    fTmpA = fZ * (4.683325804901025f * fZ2 + -2.007139630671868f);
    pSH[21] = fTmpA * fC0;
    pSH[19] = fTmpA * fS0;
    fTmpB = 2.03100960115899f * fZ * fTmpA + -0.991031208965115f * fTmpC;
    pSH[31] = fTmpB * fC0;
    pSH[29] = fTmpB * fS0;
    fTmpC = 2.021314989237028f * fZ * fTmpB + -0.9952267030562385f * fTmpA;
    pSH[43] = fTmpC * fC0;
    pSH[41] = fTmpC * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpA = 0.5462742152960395f;
    pSH[8] = fTmpA * fC1;
    pSH[4] = fTmpA * fS1;
    fTmpB = 1.445305721320277f * fZ;
    pSH[14] = fTmpB * fC1;
    pSH[10] = fTmpB * fS1;
    fTmpC = 3.31161143515146f * fZ2 + -0.47308734787878f;
    pSH[22] = fTmpC * fC1;
    pSH[18] = fTmpC * fS1;
    fTmpA = fZ * (7.190305177459987f * fZ2 + -2.396768392486662f);
    pSH[32] = fTmpA * fC1;
    pSH[28] = fTmpA * fS1;
    fTmpB = 2.11394181566097f * fZ * fTmpA + -0.9736101204623268f * fTmpC;
    pSH[44] = fTmpB * fC1;
    pSH[40] = fTmpB * fS1;
    fC0 = fX * fC1 - fY * fS1;
    fS0 = fX * fS1 + fY * fC1;

    fTmpA = 0.5900435899266435f;
    pSH[15] = fTmpA * fC0;
    pSH[9] = fTmpA * fS0;
    fTmpB = 1.770130769779931f * fZ;
    pSH[23] = fTmpB * fC0;
    pSH[17] = fTmpB * fS0;
    fTmpC = 4.403144694917254f * fZ2 + -0.4892382994352505f;
    pSH[33] = fTmpC * fC0;
    pSH[27] = fTmpC * fS0;
    fTmpA = fZ * (10.13325785466416f * fZ2 + -2.763615778544771f);
    pSH[45] = fTmpA * fC0;
    pSH[39] = fTmpA * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpA = 0.6258357354491763f;
    pSH[24] = fTmpA * fC1;
    pSH[16] = fTmpA * fS1;
    fTmpB = 2.075662314881041f * fZ;
    pSH[34] = fTmpB * fC1;
    pSH[26] = fTmpB * fS1;
    fTmpC = 5.550213908015966f * fZ2 + -0.5045649007287241f;
    pSH[46] = fTmpC * fC1;
    pSH[38] = fTmpC * fS1;
    fC0 = fX * fC1 - fY * fS1;
    fS0 = fX * fS1 + fY * fC1;

    fTmpA = 0.6563820568401703f;
    pSH[35] = fTmpA * fC0;
    pSH[25] = fTmpA * fS0;
    fTmpB = 2.366619162231753f * fZ;
    pSH[47] = fTmpB * fC0;
    pSH[37] = fTmpB * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpC = 0.6831841051919144f;
    pSH[48] = fTmpC * fC1;
    pSH[36] = fTmpC * fS1;
}

// order 7
inline void SHEval7(const float fX, const float fY, const float fZ, float *pSH)
{
    float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
    float fZ2 = fZ * fZ;

    pSH[0] = 0.2820947917738781f;
    pSH[2] = 0.4886025119029199f * fZ;
    pSH[6] = 0.9461746957575601f * fZ2 + -0.31539156525252f;
    pSH[12] = fZ * (1.865881662950577f * fZ2 + -1.119528997770346f);
    pSH[20] = 1.984313483298443f * fZ * pSH[12] + -1.006230589874905f * pSH[6];
    pSH[30] = 1.98997487421324f * fZ * pSH[20] + -1.002853072844814f * pSH[12];
    pSH[42] = 1.993043457183566f * fZ * pSH[30] + -1.001542020962219f * pSH[20];
    pSH[56] = 1.994891434824135f * fZ * pSH[42] + -1.000927213921958f * pSH[30];
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
    fTmpA = fZ * (4.683325804901025f * fZ2 + -2.007139630671868f);
    pSH[21] = fTmpA * fC0;
    pSH[19] = fTmpA * fS0;
    fTmpB = 2.03100960115899f * fZ * fTmpA + -0.991031208965115f * fTmpC;
    pSH[31] = fTmpB * fC0;
    pSH[29] = fTmpB * fS0;
    fTmpC = 2.021314989237028f * fZ * fTmpB + -0.9952267030562385f * fTmpA;
    pSH[43] = fTmpC * fC0;
    pSH[41] = fTmpC * fS0;
    fTmpA = 2.015564437074638f * fZ * fTmpC + -0.9971550440218319f * fTmpB;
    pSH[57] = fTmpA * fC0;
    pSH[55] = fTmpA * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpA = 0.5462742152960395f;
    pSH[8] = fTmpA * fC1;
    pSH[4] = fTmpA * fS1;
    fTmpB = 1.445305721320277f * fZ;
    pSH[14] = fTmpB * fC1;
    pSH[10] = fTmpB * fS1;
    fTmpC = 3.31161143515146f * fZ2 + -0.47308734787878f;
    pSH[22] = fTmpC * fC1;
    pSH[18] = fTmpC * fS1;
    fTmpA = fZ * (7.190305177459987f * fZ2 + -2.396768392486662f);
    pSH[32] = fTmpA * fC1;
    pSH[28] = fTmpA * fS1;
    fTmpB = 2.11394181566097f * fZ * fTmpA + -0.9736101204623268f * fTmpC;
    pSH[44] = fTmpB * fC1;
    pSH[40] = fTmpB * fS1;
    fTmpC = 2.081665999466133f * fZ * fTmpB + -0.9847319278346618f * fTmpA;
    pSH[58] = fTmpC * fC1;
    pSH[54] = fTmpC * fS1;
    fC0 = fX * fC1 - fY * fS1;
    fS0 = fX * fS1 + fY * fC1;

    fTmpA = 0.5900435899266435f;
    pSH[15] = fTmpA * fC0;
    pSH[9] = fTmpA * fS0;
    fTmpB = 1.770130769779931f * fZ;
    pSH[23] = fTmpB * fC0;
    pSH[17] = fTmpB * fS0;
    fTmpC = 4.403144694917254f * fZ2 + -0.4892382994352505f;
    pSH[33] = fTmpC * fC0;
    pSH[27] = fTmpC * fS0;
    fTmpA = fZ * (10.13325785466416f * fZ2 + -2.763615778544771f);
    pSH[45] = fTmpA * fC0;
    pSH[39] = fTmpA * fS0;
    fTmpB = 2.207940216581961f * fZ * fTmpA + -0.959403223600247f * fTmpC;
    pSH[59] = fTmpB * fC0;
    pSH[53] = fTmpB * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpA = 0.6258357354491763f;
    pSH[24] = fTmpA * fC1;
    pSH[16] = fTmpA * fS1;
    fTmpB = 2.075662314881041f * fZ;
    pSH[34] = fTmpB * fC1;
    pSH[26] = fTmpB * fS1;
    fTmpC = 5.550213908015966f * fZ2 + -0.5045649007287241f;
    pSH[46] = fTmpC * fC1;
    pSH[38] = fTmpC * fS1;
    fTmpA = fZ * (13.49180504672677f * fZ2 + -3.113493472321562f);
    pSH[60] = fTmpA * fC1;
    pSH[52] = fTmpA * fS1;
    fC0 = fX * fC1 - fY * fS1;
    fS0 = fX * fS1 + fY * fC1;

    fTmpA = 0.6563820568401703f;
    pSH[35] = fTmpA * fC0;
    pSH[25] = fTmpA * fS0;
    fTmpB = 2.366619162231753f * fZ;
    pSH[47] = fTmpB * fC0;
    pSH[37] = fTmpB * fS0;
    fTmpC = 6.745902523363385f * fZ2 + -0.5189155787202604f;
    pSH[61] = fTmpC * fC0;
    pSH[51] = fTmpC * fS0;
    fC1 = fX * fC0 - fY * fS0;
    fS1 = fX * fS0 + fY * fC0;

    fTmpA = 0.6831841051919144f;
    pSH[48] = fTmpA * fC1;
    pSH[36] = fTmpA * fS1;
    fTmpB = 2.6459606618019f * fZ;
    pSH[62] = fTmpB * fC1;
    pSH[50] = fTmpB * fS1;
    fC0 = fX * fC1 - fY * fS1;
    fS0 = fX * fS1 + fY * fC1;

    fTmpC = 0.7071627325245963f;
    pSH[63] = fTmpC * fC0;
    pSH[49] = fTmpC * fS0;
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
            lfoamounts[i] = 15.0;
        }
        SmoothingStrategy::setValueInstant(smoothed_mix, 0.0);
    }
    void prepare(float sr)
    {
        samplerate = sr;
        smoothed_mix.setRateInMilliseconds(20.0, sr, 1.0);
    }
    float samplerate = 0.0;
    float basedelaytimes[NumFilters];
    float lforates[NumFilters];
    float lfoamounts[NumFilters];
    double phases[NumFilters];
    using SmoothingStrategy = sst::basic_blocks::dsp::LagSmoothingStrategy;
    SmoothingStrategy::smoothValue_t smoothed_mix;
    float mix = 0.0f;
    float process(size_t filterIndex, float input)
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
