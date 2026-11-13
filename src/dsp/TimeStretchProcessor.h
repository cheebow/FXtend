#pragma once

#include "../engine/SampleData.h"

#include <functional>

namespace fxtend
{

/** Describes one time-stretch render: which part of which audio, and how long
    it should become. Two jobs that compare equal produce the same audio. */
struct StretchJob
{
    SampleData::Ptr source;
    float trimStart = 0.0f;         // 0..1 of the source
    float trimEnd = 1.0f;
    bool reverse = false;
    float pitchSemitones = 0.0f;
    double targetSeconds = 1.0;

    bool operator== (const StretchJob& other) const noexcept
    {
        return source == other.source
               && juce::exactlyEqual (trimStart, other.trimStart)
               && juce::exactlyEqual (trimEnd, other.trimEnd)
               && reverse == other.reverse
               && juce::exactlyEqual (pitchSemitones, other.pitchSemitones)
               && std::abs (targetSeconds - other.targetSeconds) < 0.0005;
    }

    bool operator!= (const StretchJob& other) const noexcept { return ! (*this == other); }

    bool isValid() const noexcept { return source != nullptr && targetSeconds > 0.0; }

    /** Length of the trimmed region in seconds. */
    double getSourceSeconds() const noexcept;

    /** How much longer (> 1) or shorter (< 1) the audio becomes. */
    double getRatio() const noexcept;

    /** Beyond these ratios good quality cannot be expected (spec 5.2). */
    bool isExtreme() const noexcept
    {
        const double ratio = getRatio();
        return ratio < 0.25 || ratio > 8.0;
    }
};

/** The stretch a sample layer needs for a body of the given length, or an
    invalid job if the layer is not in Stretch mode or has nothing to stretch. */
StretchJob makeStretchJob (const SampleData::Ptr& source, const SampleLayerParams& params, double bodySeconds);

/** Time-stretches the job's region to its target length, keeping the pitch
    (apart from the requested transposition). The result has the source's
    sample rate and exactly round(targetSeconds * sampleRate) frames.

    Slow: runs offline at the highest quality. Never call on the audio thread.
    Returns null if aborted or if the job is not valid. */
SampleData::Ptr stretchSample (const StretchJob& job, const std::function<bool()>& shouldAbort = {});

} // namespace fxtend
