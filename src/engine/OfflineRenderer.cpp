#include "OfflineRenderer.h"

#include <cmath>
#include <memory>
#include <vector>

namespace fxtend
{

namespace
{
    constexpr int blockSize = 512;

    double bodyLengthQn (const RenderRequest& request) noexcept
    {
        return FxEngine::lengthInQuarterNotes (request.settings.lengthUnit, request.settings.lengthValue, request.timeSig);
    }
}

int getBodyLengthInSamples (const RenderRequest& request) noexcept
{
    const double seconds = bodyLengthQn (request) * 60.0 / std::max (1.0, request.bpm);
    return (int) std::ceil (seconds * request.sampleRate - 1.0e-6);
}

RenderResult renderOffline (const RenderRequest& request, const std::function<bool()>& shouldAbort)
{
    RenderResult result;
    result.bodySamples = getBodyLengthInSamples (request);

    const int maxTailSamples = (int) (std::max (0.0, request.maxTailSeconds) * request.sampleRate);
    const int maxSamples = result.bodySamples + maxTailSamples;

    // Rendered in pieces because the length of a Natural tail is only known once it has died away.
    std::vector<float> left, right;
    left.reserve ((size_t) result.bodySamples + (size_t) blockSize);
    right.reserve ((size_t) result.bodySamples + (size_t) blockSize);

    auto engine = std::make_unique<FxEngine>();
    engine->setSettings (request.settings);
    engine->setPatch (request.patch);
    engine->prepare (request.sampleRate, blockSize);

    const double qnPerSample = request.bpm / (60.0 * request.sampleRate);

    TransportState transport;
    transport.isPlaying = true;
    transport.hasPpq = true;
    transport.hasBpm = true;
    transport.bpm = request.bpm;
    transport.hasTimeSig = true;
    transport.timeSig = request.timeSig;

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, juce::jlimit (0.0f, 1.0f, request.velocity)), 0);

    juce::AudioBuffer<float> block (2, blockSize);
    int position = 0;

    while (position < maxSamples)
    {
        if (shouldAbort && shouldAbort())
            return result;

        const int numSamples = std::min (blockSize, maxSamples - position);
        transport.ppq = position * qnPerSample;

        SampleSources sources;

        for (size_t i = 0; i < sources.size(); ++i)
            sources[i] = { request.samples[i].get(), request.stretched[i].get() };

        engine->setSampleSources (sources);

        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, numSamples);
        engine->process (view, midi, transport);
        midi.clear();

        left.insert (left.end(), view.getReadPointer (0), view.getReadPointer (0) + numSamples);
        right.insert (right.end(), view.getReadPointer (1), view.getReadPointer (1) + numSamples);
        position += numSamples;

        if (position >= result.bodySamples && engine->isSilent())
            break;
    }

    // Drop the silence after the last audible sample of the tail (never cutting into the body).
    int length = (int) left.size();

    while (length > result.bodySamples && std::abs (left[(size_t) length - 1]) < 1.0e-5f && std::abs (right[(size_t) length - 1]) < 1.0e-5f)
        --length;

    result.audio.setSize (2, length);
    result.audio.copyFrom (0, 0, left.data(), length);
    result.audio.copyFrom (1, 0, right.data(), length);

    result.completed = true;
    return result;
}

} // namespace fxtend
