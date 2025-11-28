#pragma once

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
