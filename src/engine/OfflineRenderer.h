#pragma once

#include "FxEngine.h"

#include <functional>

namespace fxtend
{

struct RenderRequest
{
    Patch patch;
    EngineSettings settings;
    double bpm = 120.0;
    TimeSignature timeSig;
    double sampleRate = 48000.0;
    float velocity = 100.0f / 127.0f;
    double maxTailSeconds = 10.0;       // upper limit for a Natural tail

    // Audio for the sample layers; kept alive by the request for as long as the render runs.
    std::array<SampleData::Ptr, numSampleLayers> samples;
    std::array<SampleData::Ptr, numSampleLayers> stretched;
};

struct RenderResult
{
    juce::AudioBuffer<float> audio;     // body followed by any tail
    int bodySamples = 0;                // where the body ends
    bool completed = false;             // false if aborted
};

/** The tempo a request is rendered at: its bpm, kept above 1 so that a host
    reporting no tempo (0) cannot stretch the length to infinity. */
double getRenderBpm (const RenderRequest& request) noexcept;

/** Body length in seconds for a request (fixed tempo). */
double getBodyLengthInSeconds (const RenderRequest& request) noexcept;

/** Body length in samples for a request (fixed tempo). */
int getBodyLengthInSamples (const RenderRequest& request) noexcept;

/** Plays one Note On through a private engine at a fixed tempo, faster than
    real time. Uses the same code path as playback in a host. Not for the
    audio thread. shouldAbort is polled between blocks. */
RenderResult renderOffline (const RenderRequest& request, const std::function<bool()>& shouldAbort = {});

} // namespace fxtend
