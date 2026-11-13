#pragma once

#include "OfflineRenderer.h"

#include <juce_core/juce_core.h>

namespace fxtend
{

struct WavExportOptions
{
    double sampleRate = 48000.0;        // 44100, 48000, 96000 ...
    bool floatingPoint = false;         // false: 24-bit PCM, true: 32-bit float
    int velocity = 100;                 // 1..127
};

/** Renders, for every sample layer in Stretch mode, audio stretched for
    exactly this request's tempo and length, replacing whatever the request
    carried. Returns false if aborted. Not for the audio thread. */
bool prepareStretchedSamples (RenderRequest& request, const std::function<bool()>& shouldAbort = {});

/** Renders the request's FX - the body plus, in Natural mode, its tail - and
    writes it as a stereo WAV. The request's tempo is used as a fixed tempo.
    Not for the audio thread. */
juce::Result exportWav (RenderRequest request, const juce::File& target, const WavExportOptions& options,
                        const std::function<bool()>& shouldAbort = {});

} // namespace fxtend
