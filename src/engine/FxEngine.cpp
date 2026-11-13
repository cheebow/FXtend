#include "FxEngine.h"
#include "FxTemplates.h"

#include <algorithm>
#include <cmath>

namespace fxtend
{

namespace
{
    constexpr double minLengthQn = 0.25;
    constexpr double maxLengthBars = 32.0;
}

FxEngine::FxEngine()
{
    patch = makeTemplate (FxType::riser);
    effectivePatch = patch;
}

void FxEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    mixBuffer.setSize (3, std::max (16, maxBlockSize), false, false, true);
    clock.prepare (sampleRate);
    masterFx.prepare (sampleRate, maxBlockSize, applyMacros (patch).master);
    outputActive = false;
    quietSeconds = 0.0;

    for (auto& voice : voices)
        voice.prepare (sampleRate);

    newestVoice = nullptr;
}

void FxEngine::reset() noexcept
{
    killAllVoices();
    clock.reset();
    masterFx.reset();
    outputActive = false;
}

double FxEngine::lengthInQuarterNotes (LengthUnit unit, double value, TimeSignature timeSig) noexcept
{
    const int numerator = timeSig.numerator > 0 ? timeSig.numerator : 4;
    const int denominator = timeSig.denominator > 0 ? timeSig.denominator : 4;

    const double qnPerBeat = 4.0 / denominator;     // a beat is the denominator's note value
    const double qnPerBar = numerator * qnPerBeat;

    double qn = value;

    if (unit == LengthUnit::bars)
        qn = value * qnPerBar;
    else if (unit == LengthUnit::beats)
        qn = value * qnPerBeat;

    return std::clamp (qn, minLengthQn, maxLengthBars * qnPerBar);
}

void FxEngine::triggerPreview (float velocity) noexcept
{
    pendingPreviewVelocity.store (std::clamp (velocity, 0.0f, 1.0f), std::memory_order_release);
}

void FxEngine::killAllVoices() noexcept
{
    for (auto& voice : voices)
        voice.kill();

    newestVoice = nullptr;
}

void FxEngine::noteOn (int sampleOffset, float velocity, const BlockTiming& timing) noexcept
{
    if (settings.retrigger == RetriggerMode::restart)
    {
        for (auto& voice : voices)
            voice.release (sampleOffset);
    }
    else
    {
        // Overlap: keep at most maxOverlapVoices bodies, fading out the oldest.
        int bodies = 0;
        FxVoice* oldest = nullptr;

        for (auto& voice : voices)
        {
            if (voice.isInBody())
            {
                ++bodies;

                if (oldest == nullptr || voice.getAge() > oldest->getAge())
                    oldest = &voice;
            }
        }

        if (bodies >= maxOverlapVoices && oldest != nullptr)
            oldest->release (sampleOffset);
    }

    FxVoice* target = nullptr;

    for (auto& voice : voices)
    {
        if (! voice.isActive())
        {
            target = &voice;
            break;
        }
    }

    if (target == nullptr)
    {
        // Every slot is busy (only possible under a burst of retriggers):
        // take over the voice that has been sounding longest.
        target = &voices[0];

        for (auto& voice : voices)
            if (voice.getAge() > target->getAge())
                target = &voice;
    }

    VoiceStart start;
    start.startPpq = timing.ppqAt (sampleOffset);
    start.lengthQn = lengthInQuarterNotes (settings.lengthUnit, settings.lengthValue, timing.timeSig);
    start.gain = velocity * velocity;
    start.sampleOffset = sampleOffset;
    start.seed = (seedCounter += 0x9e3779b9u);

    target->start (start);
    newestVoice = target;

    status.lastNoteOnOffset.store (sampleOffset, std::memory_order_relaxed);
    status.lastNoteOnPpq.store (start.startPpq, std::memory_order_relaxed);
    status.noteOnCount.fetch_add (1, std::memory_order_relaxed);
}

void FxEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const TransportState& transport)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0)
        return;

    if (mixBuffer.getNumSamples() < numSamples)
        mixBuffer.setSize (3, numSamples, false, false, true);   // host exceeded its promised block size

    clock.setManualBpm (settings.manualBpm);
    const BlockTiming timing = clock.beginBlock (transport, numSamples);

    if (timing.discontinuity)
    {
        killAllVoices();
        status.discontinuityCount.fetch_add (1, std::memory_order_relaxed);
    }

    if (pendingStop.exchange (false, std::memory_order_acquire))
    {
        killAllVoices();
        masterFx.reset();
        outputActive = false;
    }

    const float previewVelocity = pendingPreviewVelocity.exchange (-1.0f, std::memory_order_acquire);

    if (previewVelocity > 0.0f)
        noteOn (0, previewVelocity, timing);

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())     // velocity 0 counts as a Note Off and is ignored
            noteOn (std::clamp (metadata.samplePosition, 0, numSamples - 1), message.getFloatVelocity(), timing);
    }

    float* left = mixBuffer.getWritePointer (0);
    float* right = mixBuffer.getWritePointer (1);
    float* bodyGate = mixBuffer.getWritePointer (2);
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);
    juce::FloatVectorOperations::clear (bodyGate, numSamples);

    effectivePatch = applyMacros (patch);

    bool voicesSounding = false;

    for (auto& voice : voices)
    {
        voicesSounding = voicesSounding || voice.isActive();
        voice.render (left, right, bodyGate, numSamples, timing, effectivePatch, sampleSources);
    }

    sampleSources = {};      // never keep pointers beyond the call they were given for

    int active = 0;

    for (auto& voice : voices)
        if (voice.isActive())
            ++active;

    // Master section. It is skipped entirely while nothing sounds, and its
    // delay/reverb memory is wiped when the sound has ended, so a Strict FX
    // can never leak an old tail into the next trigger.
    const bool strict = effectivePatch.tailMode == TailMode::strict;

    if (voicesSounding)
    {
        outputActive = true;
        quietSeconds = 0.0;
    }

    if (outputActive)
    {
        masterFx.process (left, right, numSamples, effectivePatch.master, effectivePatch.masterGainDb,
                          timing.bpm, bodyGate, strict);

        if (active == 0)
        {
            const bool wetAudible = ! strict && masterFx.getWetPeak() > 1.0e-4f;
            quietSeconds = wetAudible ? 0.0 : quietSeconds + numSamples / sampleRate;

            if (strict || quietSeconds > masterFx.getDelaySeconds() + 0.15)
            {
                masterFx.reset();
                outputActive = false;
            }
        }
    }

    float peakLeft = 0.0f, peakRight = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        peakLeft = std::max (peakLeft, std::abs (left[n]));
        peakRight = std::max (peakRight, std::abs (right[n]));

        if (++status.scopeCounter >= EngineStatus::scopeDecimation)
        {
            status.scopeCounter = 0;
            status.scopeLeft[(size_t) status.scopeWrite].store (left[n], std::memory_order_relaxed);
            status.scopeRight[(size_t) status.scopeWrite].store (right[n], std::memory_order_relaxed);
            status.scopeWrite = (status.scopeWrite + 1) % EngineStatus::scopeSize;
        }
    }

    status.peakLeft.store (peakLeft, std::memory_order_relaxed);
    status.peakRight.store (peakRight, std::memory_order_relaxed);
    status.tailActive.store (outputActive && active == 0, std::memory_order_relaxed);

    if (peakLeft > 1.0f || peakRight > 1.0f)
        status.clipCount.fetch_add (1, std::memory_order_relaxed);

    if (numChannels == 1)
    {
        float* out = buffer.getWritePointer (0);

        for (int n = 0; n < numSamples; ++n)
            out[n] = 0.5f * (left[n] + right[n]);
    }
    else
    {
        buffer.copyFrom (0, 0, left, numSamples);
        buffer.copyFrom (1, 0, right, numSamples);

        for (int channel = 2; channel < numChannels; ++channel)
            buffer.clear (channel, 0, numSamples);
    }

    status.bpm.store (timing.bpm, std::memory_order_relaxed);
    status.ppq.store (timing.ppqStart, std::memory_order_relaxed);
    status.timeSigNumerator.store (timing.timeSig.numerator, std::memory_order_relaxed);
    status.timeSigDenominator.store (timing.timeSig.denominator, std::memory_order_relaxed);
    status.hostSynced.store (timing.hostSynced, std::memory_order_relaxed);
    status.tempoFromHost.store (timing.tempoFromHost, std::memory_order_relaxed);
    status.timeSigFromHost.store (timing.timeSigFromHost, std::memory_order_relaxed);
    status.lengthQn.store (lengthInQuarterNotes (settings.lengthUnit, settings.lengthValue, timing.timeSig),
                           std::memory_order_relaxed);
    status.activeVoices.store (active, std::memory_order_relaxed);
    status.phase.store (newestVoice != nullptr && newestVoice->isActive() ? newestVoice->getPhase() : -1.0,
                        std::memory_order_relaxed);
}

} // namespace fxtend
