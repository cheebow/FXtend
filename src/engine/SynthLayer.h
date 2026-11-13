#pragma once

#include "Patch.h"
#include "../dsp/NoiseGenerator.h"
#include "../dsp/Oscillators.h"
#include "../dsp/StateVariableFilter.h"

#include <cstdint>

namespace fxtend
{

/** The per-voice state of one synth layer.

    A voice calls control() at a fixed control rate with its current progress,
    then render() for the samples up to the next control point. Everything that
    shapes the FX over its length (volume, pitch, filter) follows the voice
    progress, so it stretches with the musical length; attack times and the
    burst pattern of transients and claps run in real time. */
class SynthLayer
{
public:
    static constexpr int maxChunk = 16;

    void prepare (double sampleRate) noexcept;
    void start (std::uint32_t seed) noexcept;

    /** voicePhase: 0..1 progress of the voice body. numSamples: size of the chunk about to be rendered. */
    void control (const SynthLayerParams& params, double voicePhase, int numSamples) noexcept;

    /** Adds numSamples (<= maxChunk) to left/right. */
    void render (float* left, float* right, int numSamples) noexcept;

private:
    enum class Stage { waiting, playing, ending, done };

    float burstEnvelope (bool sustainLast) noexcept;
    void beginPlaying() noexcept;

    double sampleRate = 44100.0;
    Stage stage = Stage::waiting;
    std::uint32_t seed = 1;
    std::int64_t samplesPlayed = 0;

    // Values held for the current chunk.
    SynthType type = SynthType::noise;
    int filterMode = 0;
    int variant = 0;
    float width = 1.0f;
    float drive = 0.0f;
    float driveGain = 1.0f;
    float driveNormalise = 1.0f;
    double cyclesPerSample = 0.0;
    double detune = 0.0;
    double fmRatio = 2.0;
    double fmIndex = 0.0;
    SvfCoefficients filter;

    // Gain ramps across the chunk to avoid zipper noise.
    float gainLeft = 0.0f, gainRight = 0.0f;
    float gainLeftStep = 0.0f, gainRightStep = 0.0f;
    float endingStep = 0.0f;
    int endingSamples = 0;

    // Burst pattern (Transient / Clap), in samples.
    int burstCount = 1;
    double burstSpacing = 1.0;
    float burstDecay = 0.999f;      // per-sample multiplier
    int burstIndex = 0;
    double nextBurstAt = 0.0;
    float burstLevel = 0.0f;
    bool burstSustaining = false;
    float decayLevel = 1.0f;        // real-time decay (Amp Decay)
    float decayStep = 1.0f;         // per-sample multiplier, 1 = off

    double lfoPhase = 0.0;          // 0..1
    float lfoHeld = 0.0f;           // current value of the Random shape

    NoiseGenerator noiseLeft, noiseRight, jitter, lfoRandom;
    StateVariableFilter filterLeft, filterRight;
    SubOscillator sub;
    FmOperatorPair fmLeft, fmRight;
    double clickPhase = 0.0;
};

} // namespace fxtend
