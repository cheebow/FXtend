#include "SampleLayer.h"

#include <algorithm>
#include <cmath>

namespace fxtend
{

namespace
{
    constexpr double layerEndFadeSeconds = 0.003;
    constexpr double halfPi = 1.5707963267948966;
    constexpr double maxLoopCrossfadeSeconds = 0.05;

    // How quickly a phase-locked layer pulls back onto the host timeline after
    // a small drift, and how far off it may be before it simply jumps.
    constexpr double resyncSamples = 2048.0;
    constexpr double jumpThresholdSamples = 4096.0;

    float hermite (float y0, float y1, float y2, float y3, float t) noexcept
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
}

void SampleLayer::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    stage = Stage::done;
    buffer = nullptr;
    rawInUse = stretchedInUse = nullptr;
}

void SampleLayer::start() noexcept
{
    stage = Stage::waiting;
    samplesPlayed = 0;
    buffer = nullptr;
    rawInUse = stretchedInUse = nullptr;
    needsReposition = true;
    gainLeft = gainRight = gainLeftStep = gainRightStep = 0.0f;
}

void SampleLayer::beginBlock (const SampleSource& source) noexcept
{
    if (source.raw != rawInUse || source.stretched != stretchedInUse)
    {
        // Stay silent until the next control point has looked at the new audio.
        buffer = nullptr;
        rawInUse = stretchedInUse = nullptr;
    }
}

void SampleLayer::control (const SampleLayerParams& params, const SampleSource& source, double voicePhase,
                           double bodySamples, int numSamples) noexcept
{
    const double layerStart = std::clamp ((double) params.start, 0.0, 1.0);
    const double layerEnd = std::max (layerStart + 1.0e-4, std::min (1.0, (double) params.end));
    const double progress = (voicePhase - layerStart) / (layerEnd - layerStart);
    const bool available = params.isEnabled() && source.raw != nullptr && source.raw->getNumFrames() > 16;

    bool justStarted = false;

    if (stage == Stage::waiting && available && progress >= 0.0 && progress < 1.0)
    {
        stage = Stage::playing;
        samplesPlayed = 0;
        endingSamples = 0;
        firstLoopCycle = true;
        needsReposition = true;
        justStarted = true;
    }
    else if (stage == Stage::playing && (progress >= 1.0 || ! available))
    {
        stage = Stage::ending;
        endingSamples = 0;
        endingStep = (float) (1.0 / std::max (1.0, layerEndFadeSeconds * sampleRate));
    }

    if ((stage != Stage::playing && stage != Stage::ending) || source.raw == nullptr)
    {
        buffer = nullptr;
        return;
    }

    // ---- Decide what to read ---------------------------------------------
    const auto mode = params.getMode();
    const bool useStretched = mode == SampleMode::stretch && source.stretched != nullptr
                              && source.stretched->getNumFrames() > 16;
    const SampleData& data = useStretched ? *source.stretched : *source.raw;
    const auto* newBuffer = &data.audio;

    if (newBuffer != buffer || source.raw != rawInUse || source.stretched != stretchedInUse)
        needsReposition = true;

    buffer = newBuffer;
    rawInUse = source.raw;
    stretchedInUse = source.stretched;

    const double frames = data.getNumFrames();

    if (useStretched)
    {
        // Trim, reverse and pitch are already part of the stretched audio.
        regionStart = 0.0;
        regionLength = frames;
        reversed = false;
    }
    else
    {
        const double trimStart = std::clamp ((double) params.trimStart, 0.0, 1.0) * frames;
        const double trimEnd = std::clamp ((double) params.trimEnd, 0.0, 1.0) * frames;
        regionStart = std::min (trimStart, frames - 16.0);
        regionLength = std::clamp (trimEnd - regionStart, 16.0, frames - regionStart);
        reversed = params.isReversed();
    }

    phaseLocked = mode == SampleMode::stretch || mode == SampleMode::resample;
    looping = mode == SampleMode::loop;

    const double x = std::clamp (progress, 0.0, 1.0);
    const double layerSamples = std::max (1.0, bodySamples * (layerEnd - layerStart));
    double remainingSamples = (1.0 - x) * layerSamples;

    if (phaseLocked)
    {
        const double nominal = regionLength / layerSamples;
        const double target = x * regionLength;

        if (needsReposition || std::abs (target - position) > jumpThresholdSamples * nominal)
            position = target;

        increment = std::max (0.0, nominal + (target - position) / resyncSamples);
    }
    else
    {
        increment = data.sampleRate / sampleRate * std::pow (2.0, (double) params.pitch / 12.0);
        loopCrossfade = std::min (maxLoopCrossfadeSeconds * data.sampleRate, regionLength * 0.25);

        if (needsReposition)
        {
            // Pick up where the layer would be had it played from its start.
            position = (double) samplesPlayed * increment;

            if (looping)
                position = std::fmod (position, std::max (1.0, regionLength - loopCrossfade));
        }

        if (! looping)
            remainingSamples = std::min (remainingSamples, std::max (0.0, regionLength - position) / std::max (1.0e-9, increment));
    }

    needsReposition = false;
    width = std::clamp (params.width, 0.0f, 1.0f);

    // ---- Volume -----------------------------------------------------------
    float amp = std::max (0.0f, params.ampAt ((float) x));

    if (params.fadeInMs > 0.0f)
        amp *= (float) std::min (1.0, (double) (samplesPlayed + numSamples) / (params.fadeInMs * 0.001 * sampleRate));

    if (params.fadeOutMs > 0.0f)
        amp *= (float) std::min (1.0, remainingSamples / (params.fadeOutMs * 0.001 * sampleRate));

    const float level = std::pow (10.0f, params.levelDb / 20.0f) * amp;
    const double angle = (std::clamp ((double) params.pan, -1.0, 1.0) + 1.0) * 0.5 * halfPi;
    const float targetLeft = level * (float) (std::cos (angle) * 1.4142135623730951);
    const float targetRight = level * (float) (std::sin (angle) * 1.4142135623730951);

    if (justStarted && params.fadeInMs <= 0.0f)
    {
        gainLeft = targetLeft;
        gainRight = targetRight;
        gainLeftStep = gainRightStep = 0.0f;
    }
    else
    {
        const float invSamples = 1.0f / (float) std::max (1, numSamples);
        gainLeftStep = (targetLeft - gainLeft) * invSamples;
        gainRightStep = (targetRight - gainRight) * invSamples;
    }
}

void SampleLayer::readFrame (double offset, float& left, float& right) const noexcept
{
    const double clamped = std::clamp (offset, 0.0, regionLength - 1.0);
    const double absolute = regionStart + (reversed ? regionLength - 1.0 - clamped : clamped);

    const int lastFrame = buffer->getNumSamples() - 1;
    const int index = std::clamp ((int) absolute, 0, lastFrame);
    const auto t = (float) (absolute - (double) index);

    const int i0 = std::max (0, index - 1);
    const int i2 = std::min (lastFrame, index + 1);
    const int i3 = std::min (lastFrame, index + 2);

    const float* l = buffer->getReadPointer (0);
    const float* r = buffer->getReadPointer (1);

    left = hermite (l[i0], l[index], l[i2], l[i3], t);
    right = hermite (r[i0], r[index], r[i2], r[i3], t);
}

void SampleLayer::render (float* left, float* right, int numSamples) noexcept
{
    if ((stage != Stage::playing && stage != Stage::ending) || buffer == nullptr)
    {
        if (stage == Stage::playing || stage == Stage::ending)
            samplesPlayed += numSamples;        // keep time while the audio is unavailable

        return;
    }

    const double loopLength = std::max (1.0, regionLength - loopCrossfade);

    for (int n = 0; n < numSamples; ++n)
    {
        float a = 0.0f, b = 0.0f;

        if (looping)
        {
            if (position >= loopLength)
            {
                position -= loopLength;
                firstLoopCycle = false;
            }

            readFrame (position, a, b);

            // Blend the start of each repeat with what follows the loop end,
            // so the join is seamless.
            if (! firstLoopCycle && position < loopCrossfade && loopCrossfade > 1.0)
            {
                float tailA = 0.0f, tailB = 0.0f;
                readFrame (loopLength + position, tailA, tailB);
                const auto mix = (float) (position / loopCrossfade);
                a = a * mix + tailA * (1.0f - mix);
                b = b * mix + tailB * (1.0f - mix);
            }
        }
        else if (position < regionLength)
        {
            readFrame (position, a, b);
        }
        else if (! phaseLocked)
        {
            stage = Stage::done;                // one-shot ran out
            return;
        }

        position += increment;

        const float mid = 0.5f * (a + b);
        const float side = 0.5f * (a - b) * width;
        a = mid + side;
        b = mid - side;

        gainLeft += gainLeftStep;
        gainRight += gainRightStep;

        float fade = 1.0f;

        if (stage == Stage::ending)
        {
            fade = std::max (0.0f, 1.0f - endingStep * (float) (endingSamples + n + 1));

            if (fade <= 0.0f)
            {
                stage = Stage::done;
                return;
            }
        }

        left[n] += a * gainLeft * fade;
        right[n] += b * gainRight * fade;
        ++samplesPlayed;
    }

    if (stage == Stage::ending)
        endingSamples += numSamples;
}

} // namespace fxtend
