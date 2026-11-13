#include "TimeStretchProcessor.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace fxtend
{

namespace
{
    constexpr int minimumFrames = 64;
    constexpr int chunkFrames = 2048;

    // Longer results are not rendered (the layer then plays at variable speed
    // instead): at very slow tempos a render would need gigabytes of memory.
    constexpr double maxTargetSeconds = 300.0;

    struct Region
    {
        int start = 0;
        int length = 0;
    };

    Region getRegion (const StretchJob& job) noexcept
    {
        if (job.source == nullptr)
            return {};

        const int frames = job.source->getNumFrames();
        const int start = std::clamp ((int) std::floor (std::clamp (job.trimStart, 0.0f, 1.0f) * (float) frames), 0, std::max (0, frames - 1));
        const int end = std::clamp ((int) std::ceil (std::clamp (job.trimEnd, 0.0f, 1.0f) * (float) frames), start, frames);
        return { start, end - start };
    }
}

double StretchJob::getSourceSeconds() const noexcept
{
    return source != nullptr && source->sampleRate > 0.0 ? getRegion (*this).length / source->sampleRate : 0.0;
}

double StretchJob::getRatio() const noexcept
{
    const double sourceSeconds = getSourceSeconds();
    return sourceSeconds > 0.0 ? targetSeconds / sourceSeconds : 1.0;
}

StretchJob makeStretchJob (const SampleData::Ptr& source, const SampleLayerParams& params, double bodySeconds)
{
    StretchJob job;
    const double portion = std::clamp ((double) params.end, 0.0, 1.0) - std::clamp ((double) params.start, 0.0, 1.0);

    if (source == nullptr || ! params.isEnabled() || params.getMode() != SampleMode::stretch || portion <= 0.001)
        return job;

    job.source = source;
    job.trimStart = params.trimStart;
    job.trimEnd = params.trimEnd;
    job.reverse = params.isReversed();
    job.pitchSemitones = params.pitch;
    job.targetSeconds = bodySeconds * portion;

    if (job.getSourceSeconds() < 0.005)
        return {};

    // The same limits as stretchSample, so a job it would refuse never counts
    // as "wanted" (the editor would wait for its render for ever).
    if (getRegion (job).length < minimumFrames
        || job.targetSeconds * source->sampleRate < (double) minimumFrames
        || job.targetSeconds > maxTargetSeconds)
        return {};

    return job;
}

SampleData::Ptr stretchSample (const StretchJob& job, const std::function<bool()>& shouldAbort)
{
    if (! job.isValid())
        return {};

    const auto region = getRegion (job);
    const double sampleRate = job.source->sampleRate;
    if (job.targetSeconds > maxTargetSeconds)
        return {};

    const int targetFrames = (int) std::llround (job.targetSeconds * sampleRate);

    if (region.length < minimumFrames || targetFrames < minimumFrames)
        return {};

    // The region to stretch, turned round first if it is to play backwards.
    juce::AudioBuffer<float> input (2, region.length);

    for (int channel = 0; channel < 2; ++channel)
    {
        input.copyFrom (channel, 0, job.source->audio, channel, region.start, region.length);

        if (job.reverse)
            input.reverse (channel, 0, region.length);
    }

    const double timeRatio = (double) targetFrames / (double) region.length;
    const double pitchScale = std::pow (2.0, (double) job.pitchSemitones / 12.0);

    using RubberBand::RubberBandStretcher;

    RubberBandStretcher stretcher ((size_t) std::llround (sampleRate), 2,
                                   RubberBandStretcher::OptionProcessOffline
                                       | RubberBandStretcher::OptionEngineFiner
                                       | RubberBandStretcher::OptionChannelsTogether,
                                   timeRatio, pitchScale);

    stretcher.setExpectedInputDuration ((size_t) region.length);
    stretcher.setMaxProcessSize ((size_t) chunkFrames);

    const auto aborted = [&shouldAbort] { return shouldAbort && shouldAbort(); };

    // Pass 1: let the stretcher look at the whole region.
    for (int position = 0; position < region.length; position += chunkFrames)
    {
        if (aborted())
            return {};

        const int count = std::min (chunkFrames, region.length - position);
        const float* channels[2] = { input.getReadPointer (0, position), input.getReadPointer (1, position) };
        stretcher.study (channels, (size_t) count, position + count >= region.length);
    }

    // Pass 2: stretch, collecting whatever comes out.
    SampleData::Ptr result (new SampleData());
    result->sampleRate = sampleRate;
    result->audio.setSize (2, targetFrames);
    result->audio.clear();

    int written = 0;
    juce::AudioBuffer<float> scratch (2, chunkFrames);

    const auto drain = [&]
    {
        for (;;)
        {
            const int available = stretcher.available();

            if (available <= 0)
                break;

            const int count = std::min (available, chunkFrames);
            float* channels[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
            const int retrieved = (int) stretcher.retrieve (channels, (size_t) count);

            if (retrieved <= 0)
                break;

            const int toCopy = std::min (retrieved, targetFrames - written);

            for (int channel = 0; channel < 2 && toCopy > 0; ++channel)
                result->audio.copyFrom (channel, written, scratch, channel, 0, toCopy);

            written += std::max (0, toCopy);
        }
    };

    for (int position = 0; position < region.length; position += chunkFrames)
    {
        if (aborted())
            return {};

        const int count = std::min (chunkFrames, region.length - position);
        const float* channels[2] = { input.getReadPointer (0, position), input.getReadPointer (1, position) };
        stretcher.process (channels, (size_t) count, position + count >= region.length);
        drain();
    }

    drain();

    // The stretcher may come out a few frames short of the requested length;
    // the remainder stays silent, so the result is always exactly as long as asked.
    return result;
}

} // namespace fxtend
