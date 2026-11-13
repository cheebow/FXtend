#pragma once

#include <cmath>

namespace fxtend
{

/** Sine / triangle oscillator for the Sub layer. Phase is kept in cycles (0..1). */
class SubOscillator
{
public:
    void reset() noexcept { phase = 0.0; }

    float next (double cyclesPerSample, bool triangle) noexcept
    {
        const double p = phase;
        phase += cyclesPerSample;
        phase -= std::floor (phase);

        if (triangle)
            return (float) (p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0));

        return (float) std::sin (6.283185307179586 * p);
    }

private:
    double phase = 0.0;
};

/** Two-operator FM: one sine modulating the phase of another. */
class FmOperatorPair
{
public:
    void reset() noexcept { carrier = modulator = 0.0; }

    float next (double carrierCyclesPerSample, double ratio, double index) noexcept
    {
        const double twoPi = 6.283185307179586;
        const float out = (float) std::sin (twoPi * carrier + index * std::sin (twoPi * modulator));

        carrier += carrierCyclesPerSample;
        carrier -= std::floor (carrier);
        modulator += carrierCyclesPerSample * ratio;
        modulator -= std::floor (modulator);

        return out;
    }

private:
    double carrier = 0.0, modulator = 0.0;
};

} // namespace fxtend
