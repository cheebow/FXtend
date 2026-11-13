#pragma once

#include <algorithm>
#include <cmath>

namespace fxtend
{

/** Coefficients for the TPT state variable filter, shared by both channels of a layer. */
struct SvfCoefficients
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;

    /** resonance: 0..1, mapped to Q 0.5..12. */
    void set (double cutoffHz, double resonance, double sampleRate) noexcept
    {
        const double limited = std::clamp (cutoffHz, 16.0, 0.45 * sampleRate);
        const double g = std::tan (3.141592653589793 * limited / sampleRate);
        const double q = 0.5 * std::pow (24.0, std::clamp (resonance, 0.0, 1.0));

        k = (float) (1.0 / q);
        a1 = (float) (1.0 / (1.0 + g * (g + 1.0 / q)));
        a2 = (float) (g * a1);
        a3 = (float) (g * a2);
    }
};

/** Topology-preserving state variable filter (Simper). Stable under fast cutoff modulation. */
class StateVariableFilter
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** mode: 1 low-pass, 2 high-pass, 3 band-pass (matches FilterType). */
    float process (float x, const SvfCoefficients& c, int mode) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        switch (mode)
        {
            case 1:  return v2;
            case 2:  return x - c.k * v1 - v2;
            case 3:  return v1;
            default: return x;
        }
    }

private:
    float ic1 = 0.0f, ic2 = 0.0f;
};

} // namespace fxtend
