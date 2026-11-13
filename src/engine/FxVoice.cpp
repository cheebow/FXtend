#include "FxVoice.h"

#include <algorithm>
#include <cmath>

namespace fxtend
{

namespace
{
    constexpr double endFadeSeconds = 0.003;
    constexpr double releaseSeconds = 0.005;
    constexpr int controlInterval = SynthLayer::maxChunk;
}

void FxVoice::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    endFadeSamples = std::max (1.0, endFadeSeconds * sampleRate);
    releaseStep = (float) (1.0 / std::max (1.0, releaseSeconds * sampleRate));

    for (auto& layer : synthLayers)
        layer.prepare (sampleRate);

    for (auto& layer : sampleLayers)
        layer.prepare (sampleRate);

    kill();
}

void FxVoice::start (const VoiceStart& s) noexcept
{
    startPpq = s.startPpq;
    lengthQn = std::max (1.0e-6, s.lengthQn);
    endPpq = startPpq + lengthQn;
    gain = s.gain;
    pendingStartOffset = std::max (0, s.sampleOffset);
    pendingReleaseOffset = 0;
    releaseGain = 1.0f;
    phase = 0.0;
    age = 0;

    std::uint32_t layerSeed = s.seed != 0 ? s.seed : 1u;

    for (auto& layer : synthLayers)
        layer.start (layerSeed += 0x9e3779b9u);

    for (auto& layer : sampleLayers)
        layer.start();

    state = State::body;
}

void FxVoice::release (int sampleOffset) noexcept
{
    if (state == State::body)
    {
        state = State::releasing;
        pendingReleaseOffset = std::max (0, sampleOffset);
    }
}

void FxVoice::kill() noexcept
{
    state = State::idle;
    pendingStartOffset = 0;
    pendingReleaseOffset = 0;
    phase = 0.0;
}

void FxVoice::render (float* left, float* right, float* bodyGate, int numSamples, const BlockTiming& timing,
                      const Patch& patch, const SampleSources& sources) noexcept
{
    if (state == State::idle)
        return;

    const int first = std::min (pendingStartOffset, numSamples);
    pendingStartOffset -= first;

    if (first >= numSamples)
        return;

    // Sample position (relative to this block) at which the body ends.
    const double endPosition = (endPpq - timing.ppqStart) / timing.qnPerSample;
    const int last = (int) std::clamp (std::ceil (endPosition - 1.0e-6), 0.0, (double) numSamples);

    int releaseFrom = numSamples;

    if (state == State::releasing)
    {
        releaseFrom = std::clamp (pendingReleaseOffset, 0, numSamples);
        pendingReleaseOffset = std::max (0, pendingReleaseOffset - numSamples);
    }

    for (size_t i = 0; i < sampleLayers.size(); ++i)
        sampleLayers[i].beginBlock (sources[i]);

    const double bodySamples = lengthQn / timing.qnPerSample;      // at the current tempo

    bool finished = false;
    int n = first;

    while (n < last && ! finished)
    {
        // Control points sit on a fixed grid counted from the voice start, so
        // the result does not depend on how the host slices its buffers.
        const int untilControl = controlInterval - (int) (age % (std::uint64_t) controlInterval);
        const int chunk = std::min (untilControl, last - n);

        float chunkLeft[SynthLayer::maxChunk] = {};
        float chunkRight[SynthLayer::maxChunk] = {};

        // Only update on the grid: a chunk cut short by a buffer boundary
        // carries on with the ramps it already has.
        if (untilControl == controlInterval)
        {
            phase = (timing.ppqAt (n) - startPpq) / lengthQn;

            for (size_t i = 0; i < synthLayers.size(); ++i)
                synthLayers[i].control (patch.synth[i], phase, controlInterval);

            for (size_t i = 0; i < sampleLayers.size(); ++i)
                sampleLayers[i].control (patch.samples[i], sources[i], phase, bodySamples, controlInterval);
        }

        for (auto& layer : synthLayers)
            layer.render (chunkLeft, chunkRight, chunk);

        for (auto& layer : sampleLayers)
            layer.render (chunkLeft, chunkRight, chunk);

        for (int i = 0; i < chunk; ++i)
        {
            const int position = n + i;
            float envelope = (float) std::min (1.0, (endPosition - position) / endFadeSamples);

            if (position >= releaseFrom)
            {
                releaseGain -= releaseStep;

                if (releaseGain <= 0.0f)
                {
                    finished = true;
                    break;
                }

                envelope *= releaseGain;
            }

            bodyGate[position] = std::max (bodyGate[position], envelope);
            left[position] += gain * envelope * chunkLeft[i];
            right[position] += gain * envelope * chunkRight[i];
        }

        n += chunk;
        age += (std::uint64_t) chunk;
    }

    if (finished || last < numSamples)
        kill();
    else
        phase = (timing.ppqAt (numSamples) - startPpq) / lengthQn;
}

} // namespace fxtend
