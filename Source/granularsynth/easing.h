//
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
//
// For more information, please refer to <https://unlicense.org>
//

#ifndef AH_EASING_H
#define AH_EASING_H

#include <cassert>
#include <algorithm>

using AHFloat = float;

// #if defined __cplusplus

// #endif

typedef AHFloat (*AHEasingFunction)(AHFloat);

// Linear interpolation (no easing)
AHFloat LinearInterpolation(AHFloat p);

// Quadratic easing; p^2
AHFloat QuadraticEaseIn(AHFloat p);
AHFloat QuadraticEaseOut(AHFloat p);
AHFloat QuadraticEaseInOut(AHFloat p);

// Cubic easing; p^3
AHFloat CubicEaseIn(AHFloat p);
AHFloat CubicEaseOut(AHFloat p);
AHFloat CubicEaseInOut(AHFloat p);

// Quartic easing; p^4
AHFloat QuarticEaseIn(AHFloat p);
AHFloat QuarticEaseOut(AHFloat p);
AHFloat QuarticEaseInOut(AHFloat p);

// Quintic easing; p^5
AHFloat QuinticEaseIn(AHFloat p);
AHFloat QuinticEaseOut(AHFloat p);
AHFloat QuinticEaseInOut(AHFloat p);

// Sine wave easing; sin(p * PI/2)
AHFloat SineEaseIn(AHFloat p);
AHFloat SineEaseOut(AHFloat p);
AHFloat SineEaseInOut(AHFloat p);

// Circular easing; sqrt(1 - p^2)
AHFloat CircularEaseIn(AHFloat p);
AHFloat CircularEaseOut(AHFloat p);
AHFloat CircularEaseInOut(AHFloat p);

// Exponential easing, base 2
AHFloat ExponentialEaseIn(AHFloat p);
AHFloat ExponentialEaseOut(AHFloat p);
AHFloat ExponentialEaseInOut(AHFloat p);

// Exponentially-damped sine wave easing
AHFloat ElasticEaseIn(AHFloat p);
AHFloat ElasticEaseOut(AHFloat p);
AHFloat ElasticEaseInOut(AHFloat p);

// Overshooting cubic easing;
AHFloat BackEaseIn(AHFloat p);
AHFloat BackEaseOut(AHFloat p);
AHFloat BackEaseInOut(AHFloat p);

// Exponentially-decaying bounce easing
AHFloat BounceEaseOut(AHFloat p);
AHFloat BounceEaseIn(AHFloat p);

AHFloat BounceEaseInOut(AHFloat p);

typedef struct
{
    const char *name;
    AHEasingFunction function;
} EasingMapping;

const EasingMapping easing_table[] = {
    {"Linear", LinearInterpolation},

    {"QuadraticEaseIn", QuadraticEaseIn},
    {"QuadraticEaseOut", QuadraticEaseOut},
    {"QuadraticEaseInOut", QuadraticEaseInOut},

    {"CubicEaseIn", CubicEaseIn},
    {"CubicEaseOut", CubicEaseOut},
    {"CubicEaseInOut", CubicEaseInOut},

    {"QuarticEaseIn", QuarticEaseIn},
    {"QuarticEaseOut", QuarticEaseOut},
    {"QuarticEaseInOut", QuarticEaseInOut},

    {"QuinticEaseIn", QuinticEaseIn},
    {"QuinticEaseOut", QuinticEaseOut},
    {"QuinticEaseInOut", QuinticEaseInOut},

    {"SineEaseIn", SineEaseIn},
    {"SineEaseOut", SineEaseOut},
    {"SineEaseInOut", SineEaseInOut},

    {"CircularEaseIn", CircularEaseIn},
    {"CircularEaseOut", CircularEaseOut},
    {"CircularEaseInOut", CircularEaseInOut},

    {"ExponentialEaseIn", ExponentialEaseIn},
    {"ExponentialEaseOut", ExponentialEaseOut},
    {"ExponentialEaseInOut", ExponentialEaseInOut},

    {"ElasticEaseIn", ElasticEaseIn},
    {"ElasticEaseOut", ElasticEaseOut},
    {"ElasticEaseInOut", ElasticEaseInOut},

    {"BackEaseIn", BackEaseIn},
    {"BackEaseOut", BackEaseOut},
    {"BackEaseInOut", BackEaseInOut},

    {"BounceEaseIn", BounceEaseIn},
    {"BounceEaseOut", BounceEaseOut},
    {"BounceEaseInOut", BounceEaseInOut},

    {0, 0} // Sentinel to mark end of table
};

struct EasingLUTS
{
    static const size_t LUTSize = 1024;
    static const size_t numFunctions = 31;
    float data[numFunctions][LUTSize + 1];
    EasingLUTS()
    {
        for (size_t i = 0; i < numFunctions; ++i)
        {
            auto f = easing_table[i].function;
            for (size_t j = 0; j < LUTSize; ++j)
            {
                float x = 1.0 / (LUTSize - 1) * j;
                data[i][j] = f(x);
            }
            data[i][LUTSize] = data[i][LUTSize - 1];
        }
    }
    template <bool ClampInput> float getValueLERP(size_t funcindex, float x)
    {
        if constexpr (ClampInput)
            x = std::clamp(x, 0.0f, 1.0f);
        else
            assert(x >= 0.0f && x <= 1.0f);
        x *= (LUTSize - 1);
        size_t index0 = x;
        size_t index1 = index0 + 1;
        float frac = x - (int)x;
        float y0 = data[funcindex][index0];
        float y1 = data[funcindex][index1];
        return y0 + (y1 - y0) * frac;
    }
};

#endif
