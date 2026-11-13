#pragma once

#include "Patch.h"
#include "../dsp/StateVariableFilter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace fxtend
{

/** Master chain: Filter/EQ -> Drive -> Delay/Reverb -> Width/Imager -> Compressor -> Limiter.

    At the Body End the two tail modes differ: in Strict mode the whole output
    is multiplied by the body gate and is therefore silent from the end point
    on; in Natural mode the delay and reverb are left to ring out as the tail. */
class MasterFx
{
public:
    /** initialParams: the settings in effect when playback starts, so the first
        note does not hear the reverb gliding in from its defaults. */
    void prepare (double sampleRate, int maxBlockSize, const MasterParams& initialParams);

    /** Clears all delay lines, reverb tails and filter states. */
    void reset() noexcept;

    /** Processes left/right in place.
        bodyGate: per-sample 0..1, 1 while a body is sounding and falling to 0 at its end.
        gateWet:  true in Strict mode (applies the gate to the output). */
    void process (float* left, float* right, int numSamples, const MasterParams& params, float outputGainDb,
                  double bpm, const float* bodyGate, bool gateWet) noexcept;

    /** Current delay time; echoes can come back this long after the input stopped. */
    double getDelaySeconds() const noexcept { return delaySamples / sampleRate; }

    /** Peak of the wet signal in the last processed block (before the limiter). */
    float getWetPeak() const noexcept { return wetPeak; }

private:
    float readDelay (const std::vector<float>& line, double delaySamples) const noexcept;
    void setReverbParameters (const MasterParams& params);

    double sampleRate = 44100.0;

    // EQ
    SvfCoefficients highPass, lowPass;
    StateVariableFilter highPassLeft, highPassRight, lowPassLeft, lowPassRight;
    float tiltCoefficient = 0.0f;
    float tiltStateLeft = 0.0f, tiltStateRight = 0.0f;

    // Delay
    std::vector<float> delayLeft, delayRight;
    int delayWrite = 0;
    double delaySamples = 0.0;          // smoothed
    bool delayPrimed = false;

    // Reverb
    juce::Reverb reverb;
    std::vector<float> preDelayLeft, preDelayRight;     // in front of the reverb
    int preDelayWrite = 0;
    float reverbLowLeft = 0.0f, reverbLowRight = 0.0f;  // one-pole states shaping the reverb's output
    float reverbHighLeft = 0.0f, reverbHighRight = 0.0f;

    // Tape delay: filters in the feedback path.
    float tapeLowLeft = 0.0f, tapeLowRight = 0.0f;
    float tapeHighLeft = 0.0f, tapeHighRight = 0.0f;
    std::vector<float> wetLeft, wetRight, sendLeft, sendRight;

    // Bass Mono: states of the two high-passes on the side (left minus right) signal
    float sideLow1 = 0.0f, sideLow2 = 0.0f;

    // Imager: a three-band crossover. Each stage gives a low and a high part
    // that add back up to the input with only its phase turned, so the same
    // stages run on the mid signal to keep it lined up with the side signal.
    struct CrossoverStage
    {
        float s1 = 0.0f, s2 = 0.0f;

        /** Returns the low part; high receives the rest (low + high = the input, phase-shifted). */
        float split (float x, float g, float& high) noexcept
        {
            const float hp = (x - (g + 2.0f) * s1 - s2) / (1.0f + g * (g + 2.0f));
            const float bp = g * hp + s1;
            s1 = g * hp + bp;
            const float lp = g * bp + s2;
            s2 = g * bp + lp;
            high = -hp;
            return lp;
        }

        void reset() noexcept { s1 = s2 = 0.0f; }
    };

    CrossoverStage sideLowSplit, sideHighSplit, sideLowAlign, midLowSplit, midHighSplit;
    bool imagerWasOn = false;

    // Compressor
    float compEnvelope = 0.0f;
    float boostGain = 1.0f;

    // Limiter
    float limiterGain = 1.0f;
    float limiterRelease = 0.999f;
    float outputGain = 1.0f;
    bool outputGainPrimed = false;

    float wetPeak = 0.0f;
};

} // namespace fxtend
