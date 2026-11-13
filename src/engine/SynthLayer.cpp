#include "SynthLayer.h"

#include <algorithm>
#include <cmath>

namespace fxtend
{

namespace
{
    constexpr double layerEndFadeSeconds = 0.003;
    constexpr double halfPi = 1.5707963267948966;
}

void SynthLayer::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    stage = Stage::done;
}

void SynthLayer::start (std::uint32_t newSeed) noexcept
{
    seed = newSeed;
    stage = Stage::waiting;
    samplesPlayed = 0;
    gainLeft = gainRight = gainLeftStep = gainRightStep = 0.0f;
}

void SynthLayer::beginPlaying() noexcept
{
    stage = Stage::playing;
    samplesPlayed = 0;

    noiseLeft.seed (seed * 2654435761u + 1u);
    noiseRight.seed (seed * 2246822519u + 7u);
    jitter.seed (seed * 3266489917u + 13u);
    lfoRandom.seed (seed * 668265263u + 29u);
    lfoPhase = 0.0;
    lfoHeld = lfoRandom.white();
    filterLeft.reset();
    filterRight.reset();
    sub.reset();
    fmLeft.reset();
    fmRight.reset();
    clickPhase = 0.0;

    burstIndex = 0;
    nextBurstAt = 0.0;
    burstLevel = 0.0f;
    burstSustaining = false;
    decayLevel = 1.0f;
    decayStep = 1.0f;
    endingStep = 0.0f;
    endingSamples = 0;
}

void SynthLayer::control (const SynthLayerParams& params, double voicePhase, int numSamples) noexcept
{
    const double layerStart = std::clamp ((double) params.start, 0.0, 1.0);
    const double layerEnd = std::max (layerStart + 1.0e-4, std::min (1.0, (double) params.end));
    const double progress = (voicePhase - layerStart) / (layerEnd - layerStart);

    bool justStarted = false;

    if (stage == Stage::waiting && params.isEnabled() && progress >= 0.0 && progress < 1.0)
    {
        beginPlaying();
        justStarted = true;
    }
    else if (stage == Stage::playing && (progress >= 1.0 || ! params.isEnabled()))
    {
        stage = Stage::ending;
        endingSamples = 0;
        endingStep = (float) (1.0 / std::max (1.0, layerEndFadeSeconds * sampleRate));
    }

    if (stage != Stage::playing && stage != Stage::ending)
        return;

    const float x = (float) std::clamp (progress, 0.0, 1.0);

    type = params.getType();
    filterMode = (int) params.getFilterType();
    variant = params.getVariant();
    width = std::clamp (params.width, 0.0f, 1.0f);

    // LFO, evaluated once per control step (-1..1).
    float lfo = 0.0f;
    const bool useLfo = params.lfoPitchSt > 0.0f || params.lfoCutoffOct > 0.0f || params.lfoAmp > 0.0f;

    if (useLfo)
    {
        const auto phase = (float) lfoPhase;

        switch ((LfoShape) std::clamp ((int) (params.lfoShape + 0.5f), 0, 4))
        {
            case LfoShape::sine:        lfo = std::sin (6.2831853f * phase); break;
            case LfoShape::triangle:    lfo = 1.0f - 4.0f * std::abs (phase - 0.5f); break;
            case LfoShape::square:      lfo = phase < 0.5f ? 1.0f : -1.0f; break;
            case LfoShape::sawDown:     lfo = 1.0f - 2.0f * phase; break;
            case LfoShape::random:      lfo = lfoHeld; break;
        }

        const double rateStart = std::clamp ((double) params.lfoRateStart, 0.01, 100.0);
        const double rateEnd = std::clamp ((double) params.lfoRateEnd, 0.01, 100.0);
        lfoPhase += rateStart * std::pow (rateEnd / rateStart, (double) x) * (double) numSamples / sampleRate;

        if (lfoPhase >= 1.0)
        {
            lfoPhase -= std::floor (lfoPhase);
            lfoHeld = lfoRandom.white();
        }
    }

    // Pitch and oscillator rates.
    const double pitchRatio = std::pow (2.0, (double) (params.pitchAt (x) + params.lfoPitchSt * lfo) / 12.0);

    // The punch: extra pitch that falls away in real time from the start of the layer.
    double punchRatio = 1.0;

    if (params.pitchDropSt > 0.0f)
    {
        const double seconds = (double) samplesPlayed / sampleRate;
        const double semitones = (double) params.pitchDropSt * std::exp (-seconds / std::max (0.001, (double) params.pitchDropMs * 0.001));
        punchRatio = std::pow (2.0, semitones / 12.0);
    }

    decayStep = params.ampDecayMs > 0.0f ? (float) std::exp (-1.0 / std::max (1.0, (double) params.ampDecayMs * 0.001 * sampleRate))
                                         : 1.0f;

    const double frequency = std::clamp ((double) params.baseHz * pitchRatio * punchRatio, 1.0, 0.45 * sampleRate);
    cyclesPerSample = frequency / sampleRate;
    detune = 0.0015 * width;

    // Filter: sweep in octaves so it sounds even across the range.
    const float curve = params.filterCurveAt (x);
    const double cutoffStart = std::max (16.0, (double) params.cutoffStart);
    const double cutoffEnd = std::max (16.0, (double) params.cutoffEnd);
    double cutoff = cutoffStart * std::pow (cutoffEnd / cutoffStart, (double) curve);

    if (type != SynthType::sub && type != SynthType::fm)
        cutoff *= pitchRatio;       // the layer's Pitch moves the colour of noise-based sources

    if (params.lfoCutoffOct > 0.0f)
        cutoff *= std::pow (2.0, (double) (params.lfoCutoffOct * lfo));

    if (filterMode != 0)
        filter.set (cutoff, params.resonance, sampleRate);

    fmRatio = params.fmRatio;
    fmIndex = params.fmIndexStart + (params.fmIndexEnd - params.fmIndexStart) * curve;

    drive = std::clamp (params.drive, 0.0f, 1.0f);
    driveGain = 1.0f + 9.0f * drive;
    driveNormalise = 1.0f / std::tanh (driveGain);

    // Bursts.
    burstCount = std::clamp (params.getCount(), 1, 8);
    burstSpacing = std::max (1.0, (double) params.spreadMs * 0.001 * sampleRate);
    burstDecay = (float) std::exp (-1.0 / std::max (1.0, (double) params.decayMs * 0.001 * sampleRate));

    // Volume.
    float amp = std::max (0.0f, params.ampAt (x));
    amp *= 1.0f - std::clamp (params.lfoAmp, 0.0f, 1.0f) * 0.5f * (1.0f - lfo);

    if (params.attackMs > 0.0f)
    {
        const double attackSamples = params.attackMs * 0.001 * sampleRate;
        amp *= (float) std::min (1.0, (double) (samplesPlayed + numSamples) / attackSamples);
    }

    const float level = std::pow (10.0f, params.levelDb / 20.0f) * amp;
    const double angle = (std::clamp ((double) params.pan, -1.0, 1.0) + 1.0) * 0.5 * halfPi;
    const float targetLeft = level * (float) (std::cos (angle) * 1.4142135623730951);
    const float targetRight = level * (float) (std::sin (angle) * 1.4142135623730951);

    if (justStarted && params.attackMs <= 0.0f)
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

float SynthLayer::burstEnvelope (bool sustainLast) noexcept
{
    if (burstIndex < burstCount && (double) samplesPlayed >= nextBurstAt)
    {
        ++burstIndex;
        const float random = 0.5f * (jitter.white() + 1.0f);         // 0..1

        // Claps are slightly irregular, like several hands; transients are exact.
        burstLevel = sustainLast ? 0.75f + 0.25f * random : 1.0f;
        nextBurstAt += burstSpacing * (sustainLast ? 0.8 + 0.4 * (double) random : 1.0);
        burstSustaining = sustainLast && burstIndex == burstCount;

        if (burstSustaining)
            burstLevel = 1.0f;
    }

    const float value = burstLevel;

    if (! burstSustaining)
        burstLevel *= burstDecay;

    return value;
}

void SynthLayer::render (float* left, float* right, int numSamples) noexcept
{
    if (stage != Stage::playing && stage != Stage::ending)
        return;

    const auto color = (NoiseColor) std::clamp (variant, 0, 2);
    float fade = 1.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        float a = 0.0f, b = 0.0f;
        bool stereoSource = true;

        switch (type)
        {
            case SynthType::noise:
                a = noiseLeft.next (color);
                b = noiseRight.next (color);
                break;

            case SynthType::sub:
                a = b = sub.next (cyclesPerSample, variant == 1);
                stereoSource = false;
                break;

            case SynthType::fm:
                a = fmLeft.next (cyclesPerSample * (1.0 + detune), fmRatio, fmIndex);
                b = fmRight.next (cyclesPerSample * (1.0 - detune), fmRatio, fmIndex);
                stereoSource = false;       // width is applied through detune instead
                break;

            case SynthType::transient:
            {
                const float envelope = burstEnvelope (false);

                if (variant == 1)
                {
                    a = b = envelope * (float) std::sin (6.283185307179586 * clickPhase);
                    clickPhase += cyclesPerSample;
                    clickPhase -= std::floor (clickPhase);
                    stereoSource = false;
                }
                else
                {
                    a = envelope * noiseLeft.white();
                    b = envelope * noiseRight.white();
                }

                break;
            }

            case SynthType::clap:
            {
                const float envelope = burstEnvelope (true);
                a = envelope * noiseLeft.white();
                b = envelope * noiseRight.white();
                break;
            }
        }

        if (filterMode != 0)
        {
            a = filterLeft.process (a, filter, filterMode);
            b = filterRight.process (b, filter, filterMode);
        }

        if (stereoSource)
        {
            const float mid = 0.5f * (a + b);
            const float side = 0.5f * (a - b) * width;
            a = mid + side;
            b = mid - side;
        }

        if (drive > 0.0f)
        {
            a = std::tanh (a * driveGain) * driveNormalise;
            b = std::tanh (b * driveGain) * driveNormalise;
        }

        gainLeft += gainLeftStep;
        gainRight += gainRightStep;

        if (stage == Stage::ending)
        {
            fade = std::max (0.0f, 1.0f - endingStep * (float) (endingSamples + n + 1));

            if (fade <= 0.0f)
            {
                stage = Stage::done;
                return;
            }
        }

        left[n] += a * gainLeft * fade * decayLevel;
        right[n] += b * gainRight * fade * decayLevel;

        if (type != SynthType::clap || burstSustaining)
            decayLevel *= decayStep;

        ++samplesPlayed;
    }

    if (stage == Stage::ending)
        endingSamples += numSamples;
}

} // namespace fxtend
