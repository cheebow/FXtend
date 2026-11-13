#include "MasterFx.h"
#include "LayerFields.h"

#include <algorithm>
#include <cmath>

namespace fxtend
{

namespace
{
    constexpr double maxDelaySeconds = 4.0;
    constexpr float limiterThreshold = 0.944f;      // -0.5 dBFS
    constexpr double tiltPivotHz = 1000.0;
    constexpr double maxPreDelaySeconds = 0.06;

    /** What makes one reverb type differ from another. The Size and Damping
        settings move within the range the type allows. */
    struct ReverbCharacter
    {
        float sizeLow, sizeHigh;        // room size at Size = 0 and 1
        float dampingScale, dampingAdd;
        float preDelayMs;
        float lowCutHz, highCutHz;      // on the reverb's output; 0 = off
        float width;
    };

    constexpr ReverbCharacter reverbCharacters[] =
    {
        { 0.0f,  1.0f,  1.0f,  0.0f,  0.0f,  0.0f,   0.0f,    1.0f },      // Hall: the plain reverb
        { 0.15f, 0.6f,  1.0f,  0.25f, 0.0f,  180.0f, 8000.0f, 0.8f },      // Room: short and close
        { 0.45f, 0.85f, 0.35f, 0.0f,  0.0f,  350.0f, 0.0f,    1.0f },      // Plate: bright and dense, no lows
        { 0.86f, 0.99f, 0.8f,  0.0f,  35.0f, 120.0f, 6500.0f, 1.0f },      // Cathedral: very long, set back
        { 0.5f,  0.95f, 1.0f,  0.3f,  15.0f, 0.0f,   2200.0f, 1.0f },      // Dark: only the low rumble of the space
    };

    const ReverbCharacter& characterOf (const MasterParams& params) noexcept
    {
        return reverbCharacters[(size_t) std::clamp ((int) (params.reverbType + 0.5f), 0, (int) std::size (reverbCharacters) - 1)];
    }

    float onePole (double hz, double sampleRate) noexcept
    {
        return (float) (1.0 - std::exp (-6.283185307179586 * hz / sampleRate));
    }
}

void MasterFx::prepare (double newSampleRate, int maxBlockSize, const MasterParams& initialParams)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    const auto delayLength = (size_t) std::ceil (maxDelaySeconds * sampleRate) + 4;
    delayLeft.assign (delayLength, 0.0f);
    delayRight.assign (delayLength, 0.0f);

    const auto blockLength = (size_t) std::max (16, maxBlockSize);
    wetLeft.assign (blockLength, 0.0f);
    wetRight.assign (blockLength, 0.0f);
    sendLeft.assign (blockLength, 0.0f);
    sendRight.assign (blockLength, 0.0f);

    const auto preDelayLength = (size_t) std::ceil (maxPreDelaySeconds * sampleRate) + 4;
    preDelayLeft.assign (preDelayLength, 0.0f);
    preDelayRight.assign (preDelayLength, 0.0f);

    setReverbParameters (initialParams);
    reverb.setSampleRate (sampleRate);      // also snaps the reverb's internal smoothing to the values just set
    tiltCoefficient = (float) (1.0 - std::exp (-6.283185307179586 * tiltPivotHz / sampleRate));
    limiterRelease = (float) std::exp (-1.0 / (0.08 * sampleRate));
    outputGainPrimed = false;
    boostGain = initialParams.limiter >= 0.5f ? juce::Decibels::decibelsToGain (std::clamp (initialParams.limiterBoostDb, 0.0f, 18.0f)) : 1.0f;

    reset();
}

void MasterFx::reset() noexcept
{
    std::fill (delayLeft.begin(), delayLeft.end(), 0.0f);
    std::fill (delayRight.begin(), delayRight.end(), 0.0f);
    delayWrite = 0;
    delayPrimed = false;

    reverb.reset();
    std::fill (preDelayLeft.begin(), preDelayLeft.end(), 0.0f);
    std::fill (preDelayRight.begin(), preDelayRight.end(), 0.0f);
    preDelayWrite = 0;
    reverbLowLeft = reverbLowRight = reverbHighLeft = reverbHighRight = 0.0f;
    tapeLowLeft = tapeLowRight = tapeHighLeft = tapeHighRight = 0.0f;

    highPassLeft.reset();
    highPassRight.reset();
    lowPassLeft.reset();
    lowPassRight.reset();
    tiltStateLeft = tiltStateRight = 0.0f;

    limiterGain = 1.0f;
    compEnvelope = 0.0f;
    sideLow1 = sideLow2 = 0.0f;
    for (auto* stage : { &sideLowSplit, &sideHighSplit, &sideLowAlign, &midLowSplit, &midHighSplit })
        stage->reset();
    wetPeak = 0.0f;
}

void MasterFx::setReverbParameters (const MasterParams& params)
{
    juce::Reverb::Parameters reverbParameters;
    const auto& character = characterOf (params);
    const float size = std::clamp (params.reverbSize, 0.0f, 1.0f);
    reverbParameters.roomSize = character.sizeLow + (character.sizeHigh - character.sizeLow) * size;
    reverbParameters.damping = std::clamp (params.reverbDamping * character.dampingScale + character.dampingAdd, 0.0f, 1.0f);
    reverbParameters.wetLevel = 1.0f;
    reverbParameters.dryLevel = 0.0f;
    reverbParameters.width = character.width;
    reverbParameters.freezeMode = 0.0f;
    reverb.setParameters (reverbParameters);
}

float MasterFx::readDelay (const std::vector<float>& line, double delay) const noexcept
{
    const auto size = (double) line.size();
    double position = (double) delayWrite - delay;

    while (position < 0.0)
        position += size;

    const auto index = (size_t) position;
    const float fraction = (float) (position - (double) index);
    const float a = line[index % line.size()];
    const float b = line[(index + 1) % line.size()];
    return a + (b - a) * fraction;
}

void MasterFx::process (float* left, float* right, int numSamples, const MasterParams& params, float outputGainDb,
                        double bpm, const float* bodyGate, bool gateWet) noexcept
{
    if ((int) wetLeft.size() < numSamples)
    {
        // Host exceeded the block size it promised: process in pieces that fit.
        const int half = (int) wetLeft.size();
        process (left, right, half, params, outputGainDb, bpm, bodyGate, gateWet);
        process (left + half, right + half, numSamples - half, params, outputGainDb, bpm, bodyGate + half, gateWet);
        return;
    }

    // ---- Filter / EQ ------------------------------------------------------
    const bool useHighPass = params.highPassHz > 21.0f;
    const bool useLowPass = params.lowPassHz < 19900.0f;
    const bool useTilt = std::abs (params.tiltDb) > 0.01f;

    if (useHighPass)
        highPass.set (params.highPassHz, 0.1, sampleRate);

    if (useLowPass)
        lowPass.set (params.lowPassHz, 0.1, sampleRate);

    const float tiltLow = std::pow (10.0f, -params.tiltDb / 20.0f);
    const float tiltHigh = std::pow (10.0f, params.tiltDb / 20.0f);

    // ---- Drive ------------------------------------------------------------
    const float drive = std::clamp (params.drive, 0.0f, 1.0f);
    const float driveGain = 1.0f + 5.0f * drive;
    const float driveNormalise = 1.0f / std::tanh (driveGain);

    for (int n = 0; n < numSamples; ++n)
    {
        float l = left[n], r = right[n];

        if (useHighPass)
        {
            l = highPassLeft.process (l, highPass, 2);
            r = highPassRight.process (r, highPass, 2);
        }

        if (useLowPass)
        {
            l = lowPassLeft.process (l, lowPass, 1);
            r = lowPassRight.process (r, lowPass, 1);
        }

        if (useTilt)
        {
            tiltStateLeft += tiltCoefficient * (l - tiltStateLeft);
            tiltStateRight += tiltCoefficient * (r - tiltStateRight);
            l = tiltStateLeft * tiltLow + (l - tiltStateLeft) * tiltHigh;
            r = tiltStateRight * tiltLow + (r - tiltStateRight) * tiltHigh;
        }

        if (drive > 0.0f)
        {
            l = std::tanh (l * driveGain) * driveNormalise;
            r = std::tanh (r * driveGain) * driveNormalise;
        }

        left[n] = l;
        right[n] = r;
    }

    // ---- Delay ------------------------------------------------------------
    const float delayMix = std::clamp (params.delayMix, 0.0f, 1.0f);
    const float reverbMix = std::clamp (params.reverbMix, 0.0f, 1.0f);
    const float feedback = std::clamp (params.delayFeedback, 0.0f, 0.9f);

    const int timeIndex = std::clamp ((int) (params.delayTime + 0.5f), 0, (int) std::size (delayTimesQn) - 1);
    const double targetDelay = std::clamp (delayTimesQn[timeIndex] * 60.0 / std::max (20.0, bpm) * sampleRate,
                                           1.0, (double) delayLeft.size() - 4.0);

    if (! delayPrimed)
    {
        delaySamples = targetDelay;
        delayPrimed = true;
    }

    const double delaySmoothing = 1.0 - std::exp (-1.0 / (0.05 * sampleRate));
    const auto lineSize = (int) delayLeft.size();

    const auto delayType = (DelayType) std::clamp ((int) (params.delayType + 0.5f), 0, 2);
    const float tapeLow = onePole (2800.0, sampleRate);
    const float tapeHigh = onePole (160.0, sampleRate);

    for (int n = 0; n < numSamples; ++n)
    {
        delaySamples += (targetDelay - delaySamples) * delaySmoothing;

        const float delayedLeft = readDelay (delayLeft, delaySamples);
        const float delayedRight = readDelay (delayRight, delaySamples);

        // Ping-pong: each side feeds the other. Otherwise each side feeds itself.
        float backLeft = delayType == DelayType::pingPong ? delayedRight : delayedLeft;
        float backRight = delayType == DelayType::pingPong ? delayedLeft : delayedRight;

        if (delayType == DelayType::tape)
        {
            // Every repeat comes back duller, thinner and a little squashed.
            tapeLowLeft += tapeLow * (backLeft - tapeLowLeft);
            tapeLowRight += tapeLow * (backRight - tapeLowRight);
            tapeHighLeft += tapeHigh * (tapeLowLeft - tapeHighLeft);
            tapeHighRight += tapeHigh * (tapeLowRight - tapeHighRight);
            backLeft = std::tanh (1.5f * (tapeLowLeft - tapeHighLeft)) / 1.5f;
            backRight = std::tanh (1.5f * (tapeLowRight - tapeHighRight)) / 1.5f;
        }

        delayLeft[(size_t) delayWrite] = left[n] + backLeft * feedback;
        delayRight[(size_t) delayWrite] = right[n] + backRight * feedback;
        delayWrite = (delayWrite + 1) % lineSize;

        wetLeft[(size_t) n] = delayedLeft * delayMix;
        wetRight[(size_t) n] = delayedRight * delayMix;

        // The reverb hears the dry signal and the echoes.
        sendLeft[(size_t) n] = left[n] + wetLeft[(size_t) n];
        sendRight[(size_t) n] = right[n] + wetRight[(size_t) n];
    }

    // ---- Reverb -----------------------------------------------------------
    const auto& character = characterOf (params);

    if (character.preDelayMs > 0.0f)
    {
        const auto preSize = (int) preDelayLeft.size();
        const int preDelay = std::clamp ((int) (character.preDelayMs * 0.001 * sampleRate), 1, preSize - 1);

        for (int n = 0; n < numSamples; ++n)
        {
            const int read = (preDelayWrite - preDelay + preSize) % preSize;
            preDelayLeft[(size_t) preDelayWrite] = sendLeft[(size_t) n];
            preDelayRight[(size_t) preDelayWrite] = sendRight[(size_t) n];
            sendLeft[(size_t) n] = preDelayLeft[(size_t) read];
            sendRight[(size_t) n] = preDelayRight[(size_t) read];
            preDelayWrite = (preDelayWrite + 1) % preSize;
        }
    }

    setReverbParameters (params);
    reverb.processStereo (sendLeft.data(), sendRight.data(), numSamples);

    if (character.highCutHz > 0.0f)
    {
        const float coefficient = onePole (character.highCutHz, sampleRate);

        for (int n = 0; n < numSamples; ++n)
        {
            reverbLowLeft += coefficient * (sendLeft[(size_t) n] - reverbLowLeft);
            reverbLowRight += coefficient * (sendRight[(size_t) n] - reverbLowRight);
            sendLeft[(size_t) n] = reverbLowLeft;
            sendRight[(size_t) n] = reverbLowRight;
        }
    }

    if (character.lowCutHz > 0.0f)
    {
        const float coefficient = onePole (character.lowCutHz, sampleRate);

        for (int n = 0; n < numSamples; ++n)
        {
            reverbHighLeft += coefficient * (sendLeft[(size_t) n] - reverbHighLeft);
            reverbHighRight += coefficient * (sendRight[(size_t) n] - reverbHighRight);
            sendLeft[(size_t) n] -= reverbHighLeft;
            sendRight[(size_t) n] -= reverbHighRight;
        }
    }

    // ---- Mix, gate, limit -------------------------------------------------
    const bool useLimiter = params.limiter >= 0.5f;
    const float targetGain = juce::Decibels::decibelsToGain (outputGainDb, -80.0f);

    if (! outputGainPrimed)
    {
        outputGain = targetGain;
        outputGainPrimed = true;
    }

    const float gainStep = (targetGain - outputGain) / (float) numSamples;
    float peak = 0.0f;

    // Compressor: the one amount lowers the threshold and raises the ratio together.
    const float compAmount = std::clamp (params.compAmount, 0.0f, 1.0f);
    const bool useCompressor = compAmount > 1.0e-4f;
    const float compThresholdDb = -6.0f - 24.0f * compAmount;
    const float compSlope = 1.0f - 1.0f / (1.0f + 7.0f * compAmount);
    // Make-up brings a full-scale signal back to (nearly) full scale, so what
    // the compressor adds is level in the quieter parts: the body after the hit.
    const float compMakeUpDb = -compThresholdDb * compSlope * 0.9f;
    const float compAttack = (float) std::exp (-1.0 / (std::clamp ((double) params.compAttackMs, 0.1, 100.0) * 0.001 * sampleRate));
    const float compRelease = (float) std::exp (-1.0 / (std::clamp ((double) params.compReleaseMs, 20.0, 1000.0) * 0.001 * sampleRate));

    const float bassMonoHz = std::clamp (params.bassMonoHz, 0.0f, 400.0f);
    const bool useBassMono = bassMonoHz >= 20.0f;
    const float bassMonoCoefficient = (float) (1.0 - std::exp (-6.283185307179586 * bassMonoHz / sampleRate));

    const float width = std::clamp (params.width, 0.0f, 2.0f);
    const bool useWidth = std::abs (width - 1.0f) > 1.0e-4f;

    // Imager: one width per band of the side signal. With all widths at 1 it is skipped altogether.
    const float widthLow = std::clamp (params.widthLow, 0.0f, 2.0f);
    const float widthMid = std::clamp (params.widthMid, 0.0f, 2.0f);
    const float widthHigh = std::clamp (params.widthHigh, 0.0f, 2.0f);
    const bool useImager = std::abs (widthLow - 1.0f) > 1.0e-4f || std::abs (widthMid - 1.0f) > 1.0e-4f || std::abs (widthHigh - 1.0f) > 1.0e-4f;
    const float imagerLowG = (float) std::tan (3.141592653589793 * std::clamp ((double) params.imagerLowHz, 60.0, 600.0) / sampleRate);
    const float imagerHighG = (float) std::tan (3.141592653589793 * std::min (std::clamp ((double) params.imagerHighHz, 1000.0, 10000.0), 0.45 * sampleRate) / sampleRate);

    if (useImager != imagerWasOn)
    {
        for (auto* stage : { &sideLowSplit, &sideHighSplit, &sideLowAlign, &midLowSplit, &midHighSplit })
            stage->reset();

        imagerWasOn = useImager;
    }

    const float boostTarget = useLimiter ? juce::Decibels::decibelsToGain (std::clamp (params.limiterBoostDb, 0.0f, 18.0f)) : 1.0f;
    const float boostSmoothing = (float) (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));

    for (int n = 0; n < numSamples; ++n)
    {
        float wetL = wetLeft[(size_t) n] + sendLeft[(size_t) n] * reverbMix;
        float wetR = wetRight[(size_t) n] + sendRight[(size_t) n] * reverbMix;

        peak = std::max ({ peak, std::abs (wetL), std::abs (wetR) });

        outputGain += gainStep;
        float l = (left[n] + wetL) * outputGain;
        float r = (right[n] + wetR) * outputGain;

        // Width works on the finished sound, reverb and delay included, so that
        // 0 really is mono.
        if (useWidth)
        {
            const float mid = 0.5f * (l + r);
            const float side = 0.5f * (l - r) * width;
            l = mid + side;
            r = mid - side;
        }

        if (useImager)
        {
            // Side: low | rest, then rest: mid | high; the low part passes the
            // second stage whole so that all three stay lined up.
            float rest = 0.0f, high = 0.0f, lowHigh = 0.0f;
            const float lowRaw = sideLowSplit.split (0.5f * (l - r), imagerLowG, rest);
            const float midBand = sideHighSplit.split (rest, imagerHighG, high);
            const float lowBand = sideLowAlign.split (lowRaw, imagerHighG, lowHigh) + lowHigh;
            const float side = lowBand * widthLow + midBand * widthMid + high * widthHigh;

            // Mid: through the same two stages, put straight back together.
            float midRest = 0.0f, midRest2 = 0.0f;
            float centre = midLowSplit.split (0.5f * (l + r), imagerLowG, midRest) + midRest;
            centre = midHighSplit.split (centre, imagerHighG, midRest2) + midRest2;

            l = centre + side;
            r = centre - side;
        }

        if (useBassMono)
        {
            // The side (left minus right) signal goes through two high-passes
            // in a row, so what differs between the channels is only highs.
            const float mid = 0.5f * (l + r);
            float side = 0.5f * (l - r);
            sideLow1 += bassMonoCoefficient * (side - sideLow1);
            side -= sideLow1;
            sideLow2 += bassMonoCoefficient * (side - sideLow2);
            side -= sideLow2;
            l = mid + side;
            r = mid - side;
        }

        // Strict: everything - effects and the ringing of the master filters
        // included - follows the body down to silence at the end point.
        if (gateWet)
        {
            l *= bodyGate[n];
            r *= bodyGate[n];
        }

        if (useCompressor)
        {
            const float level = std::max (std::abs (l), std::abs (r));
            compEnvelope = level + (compEnvelope - level) * (level > compEnvelope ? compAttack : compRelease);

            const float overDb = 20.0f * std::log10 (compEnvelope + 1.0e-6f) - compThresholdDb;
            const float gain = std::pow (10.0f, (compMakeUpDb - std::max (0.0f, overDb) * compSlope) * 0.05f);
            l *= gain;
            r *= gain;
        }
        else
        {
            compEnvelope = 0.0f;
        }

        boostGain += (boostTarget - boostGain) * boostSmoothing;
        l *= boostGain;
        r *= boostGain;

        if (useLimiter)
        {
            const float level = std::max (std::abs (l), std::abs (r));
            const float wanted = level > limiterThreshold ? limiterThreshold / level : 1.0f;

            // Instant attack, smooth release.
            limiterGain = wanted < limiterGain ? wanted : wanted + (limiterGain - wanted) * limiterRelease;
            l *= limiterGain;
            r *= limiterGain;
        }

        left[n] = l;
        right[n] = r;
    }

    outputGain = targetGain;
    wetPeak = peak;
}

} // namespace fxtend
