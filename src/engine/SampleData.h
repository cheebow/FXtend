#pragma once

#include "Patch.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace fxtend
{

/** Decoded audio held in memory, always two channels, at its own sample rate.
    Reference counted so background renders can keep using a buffer while the
    plugin has already moved on to another one. */
struct SampleData final : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;

    int getNumFrames() const noexcept { return audio.getNumSamples(); }
};

/** What one sample slot plays from. Pointers are only valid for the duration
    of the process() call they are passed to.

    stretched, when present, is the trimmed (and reversed, pitched) region
    already time-stretched to the layer's length; the layer then simply reads
    through it in step with the FX progress. */
struct SampleSource
{
    const SampleData* raw = nullptr;
    const SampleData* stretched = nullptr;
};

using SampleSources = std::array<SampleSource, numSampleLayers>;

} // namespace fxtend
