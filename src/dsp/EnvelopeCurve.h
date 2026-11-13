#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace fxtend
{

enum class CurveShape { linear = 0, exponential, sCurve, custom };

using CurvePoints = std::array<float, 4>;   // y at x = 0, 1/3, 2/3, 1 for CurveShape::custom

inline constexpr CurvePoints defaultCurvePoints { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };

/** Maps progress x (0..1) to a curve position. Built-in shapes run from 0 to 1;
    a custom curve may go anywhere within 0..1 (e.g. up and back down).

    tension (-1..1):
      exponential  > 0 starts slow and ends fast, < 0 starts fast and ends slow
      sCurve       sharpness of the S (0 = classic smoothstep-like) */
inline float evaluateCurve (CurveShape shape, float tension, const CurvePoints& points, float x) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);

    switch (shape)
    {
        case CurveShape::linear:
            return x;

        case CurveShape::exponential:
        {
            const float k = 6.0f * std::clamp (tension, -1.0f, 1.0f);

            if (std::abs (k) < 1.0e-3f)
                return x;

            return std::expm1 (k * x) / std::expm1 (k);
        }

        case CurveShape::sCurve:
        {
            const float exponent = std::pow (2.0f, 1.0f + std::clamp (tension, -1.0f, 1.0f));
            const float a = std::pow (x, exponent);
            const float b = std::pow (1.0f - x, exponent);
            return (a + b) > 0.0f ? a / (a + b) : x;
        }

        case CurveShape::custom:
        {
            // Catmull-Rom through four evenly spaced points.
            const float scaled = x * 3.0f;
            const int segment = std::min (2, (int) scaled);
            const float t = scaled - (float) segment;

            const float p0 = points[(size_t) std::max (0, segment - 1)];
            const float p1 = points[(size_t) segment];
            const float p2 = points[(size_t) segment + 1];
            const float p3 = points[(size_t) std::min (3, segment + 2)];

            const float value = 0.5f * ((2.0f * p1)
                                        + (-p0 + p2) * t
                                        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                                        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);

            return std::clamp (value, 0.0f, 1.0f);
        }
    }

    return x;
}

} // namespace fxtend
