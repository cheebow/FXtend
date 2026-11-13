#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Storage.h"
#include "engine/FxTemplates.h"
#include "preset/PresetPackage.h"

#include <new>

using namespace fxtend;

namespace
{
    const juce::Identifier extraStateId ("Extra");
    const juce::Identifier presetNameId ("presetName");
    const juce::Identifier sampleStateId ("Sample");
    const juce::Identifier slotId ("slot");
    const juce::Identifier sampleIdId ("id");
    const juce::Identifier sampleFileNameId ("fileName");
    const juce::Identifier sampleNameId ("name");
    const juce::Identifier samplePathId ("originalPath");

    juce::String pointsToString (const CurvePoints& points)
    {
        juce::StringArray values;

        for (float point : points)
            values.add (juce::String (point, 9));

        return values.joinIntoString (" ");
    }

    void pointsFromString (const juce::String& text, CurvePoints& points)
    {
        const auto values = juce::StringArray::fromTokens (text, " ", "");

        if (values.size() == (int) points.size())
            for (size_t i = 0; i < points.size(); ++i)
                points[i] = juce::jlimit (0.0f, 1.0f, values[(int) i].getFloatValue());
    }

    juce::Identifier curveId (int layer, int curve)
    {
        static const char* const names[] = { "ampPoints", "pitchPoints", "cutoffPoints" };
        return juce::Identifier ("s" + juce::String (layer + 1) + "_" + names[curve]);
    }

    // How long an offline render waits for the stretch renders it needs.
    constexpr int offlineStretchTimeoutMs = 120000;

    bool sameMaterial (const StretchJob& a, const StretchJob& b) noexcept
    {
        StretchJob withSameLength = a;
        withSameLength.targetSeconds = b.targetSeconds;
        return withSameLength == b;
    }
}

//==============================================================================
/** Renders time-stretched audio in the background, one slot at a time. A newer
    request for a slot abandons the one in progress. */
class FXtendAudioProcessor::StretchWorker final : private juce::Thread
{
public:
    using Callback = std::function<void (int, const StretchJob&, SampleData::Ptr)>;

    explicit StretchWorker (Callback callbackToUse)
        : juce::Thread ("FXtend stretch"), callback (std::move (callbackToUse))
    {
        startThread (juce::Thread::Priority::low);
    }

    ~StretchWorker() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (15000);
    }

    void request (int slot, const StretchJob& job)
    {
        {
            const juce::ScopedLock scoped (lock);
            pending[(size_t) slot] = job;
            hasPending[(size_t) slot] = true;
            ++generation[(size_t) slot];
        }

        notify();
    }

    void cancel (int slot)
    {
        const juce::ScopedLock scoped (lock);
        pending[(size_t) slot] = {};
        hasPending[(size_t) slot] = false;
        ++generation[(size_t) slot];
    }

    bool isIdle() const
    {
        const juce::ScopedLock scoped (lock);

        for (bool waiting : hasPending)
            if (waiting)
                return false;

        return ! running;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            int slot = -1;
            int startedGeneration = 0;
            StretchJob job;

            {
                const juce::ScopedLock scoped (lock);

                for (int i = 0; i < numSampleLayers && slot < 0; ++i)
                {
                    if (hasPending[(size_t) i])
                    {
                        slot = i;
                        job = pending[(size_t) i];
                        pending[(size_t) i] = {};
                        hasPending[(size_t) i] = false;
                        startedGeneration = generation[(size_t) i];
                        running = true;
                    }
                }
            }

            if (slot < 0)
            {
                wait (-1);
                continue;
            }

            const auto superseded = [&]
            {
                const juce::ScopedLock scoped (lock);
                return generation[(size_t) slot] != startedGeneration;
            };

            SampleData::Ptr result;

            try
            {
                result = stretchSample (job, [&] { return threadShouldExit() || superseded(); });
            }
            catch (const std::bad_alloc&)
            {
                // Too big to render: the layer keeps playing at variable speed.
            }

            if (result != nullptr && ! threadShouldExit() && ! superseded())
                callback (slot, job, std::move (result));

            const juce::ScopedLock scoped (lock);
            running = false;
        }
    }

    Callback callback;
    mutable juce::CriticalSection lock;
    std::array<StretchJob, numSampleLayers> pending;
    std::array<bool, numSampleLayers> hasPending {};
    std::array<int, numSampleLayers> generation {};
    bool running = false;
};

//==============================================================================

juce::File FXtendAudioProcessor::getDefaultStorageRoot()
{
    return getAppDataDirectory();
}

namespace
{
    juce::File storageFolder (const juce::File& root, const char* name)
    {
        return (root == juce::File() ? getAppDataDirectory() : root).getChildFile (name);
    }
}

FXtendAudioProcessor::FXtendAudioProcessor (const juce::File& storageRoot)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "FXtend", createParameterLayout()),
      reader (parameters),
      presetManager (storageFolder (storageRoot, "Presets")),
      sampleManager (storageFolder (storageRoot, "Samples"))
{
    for (auto& layer : curves)
        layer.fill (defaultCurvePoints);

    audioCurves = curves;

    stretchWorker = std::make_unique<StretchWorker> ([this] (int slot, const StretchJob& job, SampleData::Ptr result)
    {
        stretchFinished (slot, job, std::move (result));
    });

    loadPreset (presetManager.indexOf ("Riser 01 Classic Noise"));
    startTimerHz (8);
}

FXtendAudioProcessor::~FXtendAudioProcessor()
{
    stopTimer();
    stretchWorker.reset();      // before the slots it reports into go away
}

void FXtendAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    Patch patch;
    EngineSettings settings;
    readCurrentSound (patch, settings);

    engine.setSettings (settings);
    engine.setPatch (patch);
    engine.prepare (sampleRate, samplesPerBlock);
}

void FXtendAudioProcessor::releaseResources()
{
    engine.reset();
}

bool FXtendAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

double FXtendAudioProcessor::getTailLengthSeconds() const
{
    // Only a Natural tail rings on after the body; give hosts room to render it.
    // Hosts ask from any thread, so this reads the one parameter and nothing else.
    return reader.readTailMode() == TailMode::natural ? 8.0 : 0.0;
}

TransportState FXtendAudioProcessor::readTransport() const
{
    TransportState state;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            state.isPlaying = position->getIsPlaying();

            if (const auto ppq = position->getPpqPosition())
            {
                state.hasPpq = true;
                state.ppq = *ppq;
            }

            if (const auto bpm = position->getBpm())
            {
                state.hasBpm = true;
                state.bpm = *bpm;
            }

            if (const auto signature = position->getTimeSignature())
            {
                state.hasTimeSig = true;
                state.timeSig = { signature->numerator, signature->denominator };
            }
        }
    }

    return state;
}

void FXtendAudioProcessor::copyCurves (const CurveSet& source, Patch& patch) noexcept
{
    for (size_t layer = 0; layer < source.size(); ++layer)
    {
        patch.synth[layer].ampPoints = source[layer][0];
        patch.synth[layer].pitchPoints = source[layer][1];
        patch.synth[layer].cutoffPoints = source[layer][2];
    }
}

void FXtendAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Some hosts send empty blocks (to flush parameter changes). There is
    // nothing to render, and nothing else here may act on one: an audition
    // would be taken for finished before it had played a sample.
    if (buffer.getNumSamples() == 0)
    {
        midi.clear();
        return;
    }

    if (curvesChanged.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock (curveLock);

        if (lock.isLocked())
        {
            audioCurves = curves;
            curvesChanged.store (false, std::memory_order_release);
        }
    }

    reader.read (audioPatch, audioSettings);
    copyCurves (audioCurves, audioPatch);

    const auto transport = readTransport();
    noteTempo (transport, audioSettings);

    // A bounce runs faster than real time and often starts the moment a project
    // has been opened - before the background stretch renders have caught up
    // with the project's tempo. Offline there is no hurry, so wait for them:
    // the file must sound like playback does.
    const bool offline = isNonRealtime();

    if (offline)
    {
        updateStretchRequests();
        waitForStretches (offlineStretchTimeoutMs);
    }

    // ---- Audition: a preset heard without being loaded ---------------------
    bool endAuditionNow = auditionCancelled.exchange (false, std::memory_order_acquire);

    if (auditionActive && ! endAuditionNow)
        for (const auto event : midi)
            if (isNoteOn (event))                       // a played note always means the real sound
                endAuditionNow = true;

    if (endAuditionNow && auditionActive)
    {
        auditionActive = false;
        engine.stopAll();
    }

    bool auditionStartsNow = false;

    if (auditionRequested.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock (auditionLock);

        if (lock.isLocked())
        {
            auditionPatch = pendingAudition;
            auditionRequested.store (false, std::memory_order_release);
            auditionActive = auditionStartsNow = true;
            auditioning.store (true, std::memory_order_release);    // in case the flag was cleared just before the request
            engine.stopAll();               // handled before the preview, inside process()
            engine.triggerPreview();
        }
    }

    engine.setSettings (audioSettings);
    engine.setPatch (auditionActive ? auditionPatch : audioPatch);

    {
        // Held for the whole block so the sample audio cannot be released
        // underneath the engine. If a sample is being swapped right now, this
        // one block plays without the sample layers instead of waiting -
        // except offline, where waiting is the right thing to do.
        const bool haveSamples = offline ? (sampleLock.enter(), true) : sampleLock.tryEnter();

        if (haveSamples)
            engine.setSampleSources (liveSources);

        engine.process (buffer, midi, transport);

        if (haveSamples)
            sampleLock.exit();
    }

    if (auditionActive && ! auditionStartsNow && engine.isSilent())
        auditionActive = false;             // it has rung out

    if (! auditionActive && ! auditionRequested.load (std::memory_order_acquire))
        auditioning.store (false, std::memory_order_release);

    midi.clear();
}

void FXtendAudioProcessor::noteTempo (const TransportState& transport, const EngineSettings& settings) noexcept
{
    // The same rules as the engine's clock: the host's tempo and time
    // signature where it reports them, the manual tempo and 4/4 otherwise.
    const bool fromHost = transport.hasBpm && transport.bpm > 0.0;
    stretchBpm.store (fromHost ? transport.bpm : juce::jlimit (20.0, 999.0, settings.manualBpm), std::memory_order_relaxed);

    const bool signatureFromHost = transport.hasTimeSig && transport.timeSig.numerator > 0 && transport.timeSig.denominator > 0;
    const TimeSignature signature = signatureFromHost ? transport.timeSig : TimeSignature {};
    stretchTimeSigNumerator.store (signature.numerator, std::memory_order_relaxed);
    stretchTimeSigDenominator.store (signature.denominator, std::memory_order_relaxed);
}

void FXtendAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Whatever was playing ends here, rather than carrying on from the middle
    // when the bypass is lifted.
    endAudition();
    engine.stopAll();
    buffer.clear();
    midi.clear();
}

juce::AudioProcessorEditor* FXtendAudioProcessor::createEditor()
{
    return new FXtendAudioProcessorEditor (*this);
}

//==============================================================================

void FXtendAudioProcessor::readCurrentSound (Patch& patch, EngineSettings& settings) const
{
    reader.read (patch, settings);

    const juce::SpinLock::ScopedLockType lock (curveLock);
    copyCurves (curves, patch);
}

Patch FXtendAudioProcessor::getCurrentPatch() const
{
    Patch patch;
    EngineSettings settings;
    readCurrentSound (patch, settings);
    return patch;
}

EngineSettings FXtendAudioProcessor::getCurrentSettings() const
{
    Patch patch;
    EngineSettings settings;
    readCurrentSound (patch, settings);
    return settings;
}

juce::String FXtendAudioProcessor::getCurrentPresetName() const
{
    const juce::ScopedLock scoped (nameLock);
    return currentPresetName;
}

void FXtendAudioProcessor::setCurrentPresetName (const juce::String& name)
{
    {
        const juce::ScopedLock scoped (nameLock);
        currentPresetName = name;
    }

    ++patchRevision;
}

void FXtendAudioProcessor::setCurves (const Patch& patch)
{
    {
        const juce::SpinLock::ScopedLockType lock (curveLock);

        for (size_t layer = 0; layer < curves.size(); ++layer)
            curves[layer] = { patch.synth[layer].ampPoints, patch.synth[layer].pitchPoints, patch.synth[layer].cutoffPoints };
    }

    curvesChanged.store (true, std::memory_order_release);
}

void FXtendAudioProcessor::setCurrentPatch (const Patch& patch)
{
    setCurves (patch);
    applyPatchToParameters (parameters, patch);
    ++patchRevision;
}

RenderRequest FXtendAudioProcessor::makeRenderRequest() const
{
    const auto& status = engine.getStatus();

    RenderRequest request;
    readCurrentSound (request.patch, request.settings);
    request.bpm = status.bpm.load();
    request.timeSig = { status.timeSigNumerator.load(), status.timeSigDenominator.load() };

    const juce::ScopedLock scoped (slotMutex);

    for (size_t i = 0; i < slots.size(); ++i)
    {
        request.samples[i] = slots[i].data;
        request.stretched[i] = slots[i].stretched;
    }

    return request;
}

PresetData FXtendAudioProcessor::getCurrentPreset (const juce::String& name) const
{
    PresetData preset;
    preset.name = name;
    preset.patch = getCurrentPatch();
    preset.sampleRefs = getSampleRefs();
    return preset;
}

juce::Result FXtendAudioProcessor::auditionPreset (int index)
{
    PresetData preset;
    const auto result = presetManager.load (index, preset);

    if (result.failed())
        return result;

    // The sample slots belong to the loaded sound, so an audition is synth only.
    for (auto& sample : preset.patch.samples)
        sample.enabled = 0.0f;

    {
        const juce::SpinLock::ScopedLockType lock (auditionLock);
        pendingAudition = preset.patch;
    }

    auditionIndex = index;
    auditionShownPatch = preset.patch;
    auditionName = preset.name;
    auditionCancelled.store (false, std::memory_order_release);
    auditioning.store (true, std::memory_order_release);
    auditionRequested.store (true, std::memory_order_release);
    return result;
}

void FXtendAudioProcessor::endAudition() noexcept
{
    auditionRequested.store (false, std::memory_order_release);

    if (auditioning.load (std::memory_order_acquire))
        auditionCancelled.store (true, std::memory_order_release);
}

bool FXtendAudioProcessor::stopForNewSound()
{
    const auto& status = engine.getStatus();
    const bool wasAuditioning = auditioning.load (std::memory_order_acquire);
    const bool wasSounding = ! wasAuditioning && (status.activeVoices.load() > 0 || status.tailActive.load());
    const bool hostRunning = status.hostSynced.load();

    endAudition();      // an audition is only stopped, never turned into a preview
    engine.stopAll();

    // With the host running, a restart would land off the grid: just stop.
    return wasSounding && ! hostRunning;
}

void FXtendAudioProcessor::restartOrStopForNewSound()
{
    if (stopForNewSound())
        engine.triggerPreview();
}

void FXtendAudioProcessor::applyPreset (const PresetData& preset)
{
    // The preview of the new sound must not start before its settings and
    // samples are in place (loading samples can take a moment), or it would
    // begin with the old samples under the new settings.
    const bool playAgain = stopForNewSound();

    setCurrentPatch (preset.patch);
    restoreSamples (preset.sampleRefs);
    hasUndoPatch = false;
    setCurrentPresetName (preset.name);

    if (playAgain)
        engine.triggerPreview();
}

juce::Result FXtendAudioProcessor::exportPresetPackage (const juce::File& target) const
{
    auto name = getCurrentPresetName();

    if (name.endsWith ("(init)"))
        name.clear();

    if (name.isEmpty())
        name = target.getFileNameWithoutExtension();

    return PresetPackage::exportPackage (getCurrentPreset (name), sampleManager, target);
}

juce::Result FXtendAudioProcessor::importPresetPackage (const juce::File& package)
{
    PresetData preset;

    if (const auto result = PresetPackage::importPackage (package, sampleManager, preset); result.failed())
        return result;

    if (preset.name.trim().isEmpty())
        preset.name = package.getFileNameWithoutExtension();

    preset.name = juce::File::createLegalFileName (preset.name.trim());

    // An import never replaces a preset that is already there, factory or the
    // user's own: it is kept beside it under a name of its own.
    if (presetManager.indexOf (preset.name) >= 0)
    {
        const auto base = preset.name + " (imported";
        preset.name = base + ")";

        for (int number = 2; presetManager.indexOf (preset.name) >= 0; ++number)
            preset.name = base + " " + juce::String (number) + ")";
    }

    applyPreset (preset);

    // The sound is loaded either way; failing to store it as a user preset is reported but not fatal.
    return presetManager.saveUser (preset.name, preset);
}

juce::Result FXtendAudioProcessor::loadPreset (int index)
{
    PresetData preset;
    const auto result = presetManager.load (index, preset);

    if (result.wasOk())
        applyPreset (preset);

    return result;
}

juce::Result FXtendAudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto result = presetManager.saveUser (name, getCurrentPreset (name));

    if (result.wasOk())
        setCurrentPresetName (name.trim());

    return result;
}

bool FXtendAudioProcessor::isCurrentPresetUserPreset() const
{
    const int index = presetManager.indexOf (getCurrentPresetName());
    return index >= 0 && ! presetManager.isFactory (index);
}

juce::Result FXtendAudioProcessor::renameCurrentPreset (const juce::String& newName)
{
    const auto result = presetManager.renameUser (presetManager.indexOf (getCurrentPresetName()), newName);

    if (result.wasOk())
        setCurrentPresetName (newName.trim());

    return result;
}

juce::Result FXtendAudioProcessor::deleteCurrentPreset()
{
    const auto result = presetManager.deleteUser (presetManager.indexOf (getCurrentPresetName()));

    // The sound stays as it is; it just no longer belongs to a saved preset.
    if (result.wasOk())
        setCurrentPresetName ({});

    return result;
}

void FXtendAudioProcessor::selectFxType (FxType type)
{
    const auto previous = getCurrentPreset (getCurrentPresetName());

    bool hadSamples = false;

    for (const auto& ref : previous.sampleRefs)
        hadSamples = hadSamples || ! ref.isEmpty();

    Patch patch = makeTemplate (type);
    patch.masterGainDb = previous.patch.masterGainDb;

    const bool playAgain = stopForNewSound();

    setCurrentPatch (patch);
    restoreSamples ({});                        // another kind of FX wants other samples

    // Losing loaded samples to one click must be reversible.
    hasUndoPatch = hadSamples;
    undoIncludesSamples = hadSamples;

    if (hadSamples)
        undoPreset = previous;

    setCurrentPresetName (juce::String (getFxTypeName (type)) + " (init)");

    if (playAgain)
        engine.triggerPreview();
}

void FXtendAudioProcessor::randomise()
{
    undoPatch = getCurrentPatch();
    hasUndoPatch = true;
    undoIncludesSamples = false;

    Patch patch = undoPatch;

    for (auto& macro : patch.macros)
        macro = 0.1f + 0.8f * random.nextFloat();

    setCurrentPatch (patch);
}

void FXtendAudioProcessor::undoRandomise()
{
    if (! hasUndoPatch)
        return;

    if (undoIncludesSamples)
    {
        const auto preset = undoPreset;
        applyPreset (preset);       // also clears the undo state
    }
    else
    {
        setCurrentPatch (undoPatch);
        hasUndoPatch = false;
    }
}

//==============================================================================
// Samples

void FXtendAudioProcessor::publishSources()
{
    // Call with slotMutex held.
    SampleSources sources;

    for (size_t i = 0; i < slots.size(); ++i)
        sources[i] = { slots[i].data.get(), slots[i].stretched.get() };

    const juce::SpinLock::ScopedLockType lock (sampleLock);     // waits at most one audio block
    liveSources = sources;
}

void FXtendAudioProcessor::installSample (int slot, const SampleRef& ref, SampleData::Ptr data, bool missing)
{
    // Old buffers are released at the end of this function, after the audio
    // thread can no longer be reading them.
    SampleData::Ptr oldData, oldStretched;

    {
        const juce::ScopedLock scoped (slotMutex);
        auto& target = slots[(size_t) slot];

        oldData = std::move (target.data);
        oldStretched = std::move (target.stretched);

        target.ref = ref;
        target.missing = missing;
        target.data = std::move (data);
        target.stretched = nullptr;
        target.stretchedJob = {};
        target.wantedJob = {};
        publishSources();

        // Under the lock, so that a request made for this slot by another
        // thread at the same moment cannot be undone by this cancel.
        stretchWorker->cancel (slot);
    }

    ++sampleRevision;
    updateStretchRequests();
}

juce::Result FXtendAudioProcessor::loadSample (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, numSampleLayers))
        return juce::Result::fail ("No such sample slot.");

    SampleRef ref;

    if (const auto imported = sampleManager.importFile (file, ref); imported.failed())
        return imported;

    juce::String error;
    auto data = sampleManager.load (ref, error);

    if (data == nullptr)
        return juce::Result::fail (error);

    installSample (slot, ref, std::move (data), false);
    notifyHostOfStateChange();
    return juce::Result::ok();
}

void FXtendAudioProcessor::clearSample (int slot)
{
    if (juce::isPositiveAndBelow (slot, numSampleLayers))
    {
        installSample (slot, {}, nullptr, false);
        notifyHostOfStateChange();
    }
}

void FXtendAudioProcessor::notifyHostOfStateChange()
{
    // Sample assignments are saved with the project but are not parameters,
    // so the host would not otherwise know there is something to save.
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void FXtendAudioProcessor::restoreSamples (const std::array<SampleRef, numSampleLayers>& refs)
{
    for (int slot = 0; slot < numSampleLayers; ++slot)
    {
        const auto& ref = refs[(size_t) slot];

        if (ref.isEmpty())
        {
            installSample (slot, {}, nullptr, false);
            continue;
        }

        {
            // Already loaded (for example when browsing presets that share a sample).
            const juce::ScopedLock scoped (slotMutex);

            if (slots[(size_t) slot].ref == ref && slots[(size_t) slot].data != nullptr)
                continue;
        }

        juce::String error;
        SampleData::Ptr data;

        if (sampleManager.locate (ref))
            data = sampleManager.load (ref, error);

        // Not found: keep the reference and report it. Never substitute another sound.
        installSample (slot, ref, data, data == nullptr);
    }
}

std::array<SampleRef, numSampleLayers> FXtendAudioProcessor::getSampleRefs() const
{
    std::array<SampleRef, numSampleLayers> refs;
    const juce::ScopedLock scoped (slotMutex);

    for (size_t i = 0; i < slots.size(); ++i)
        refs[i] = slots[i].ref;

    return refs;
}

SampleSlotInfo FXtendAudioProcessor::getSampleInfo (int slot) const
{
    SampleSlotInfo info;

    if (! juce::isPositiveAndBelow (slot, numSampleLayers))
        return info;

    const juce::ScopedLock scoped (slotMutex);
    const auto& source = slots[(size_t) slot];

    info.ref = source.ref;
    info.loaded = source.data != nullptr;
    info.missing = source.missing;
    info.seconds = source.data != nullptr ? source.data->getNumFrames() / source.data->sampleRate : 0.0;
    info.stretchMode = source.wantedJob.isValid();

    if (info.stretchMode)
    {
        info.stretchReady = source.stretched != nullptr && source.stretchedJob == source.wantedJob;
        info.stretchRatio = source.wantedJob.getRatio();
        info.stretchExtreme = source.wantedJob.isExtreme();
    }

    return info;
}

SampleData::Ptr FXtendAudioProcessor::getSampleData (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, numSampleLayers))
        return {};

    const juce::ScopedLock scoped (slotMutex);
    return slots[(size_t) slot].data;
}

void FXtendAudioProcessor::updateStretchRequests()
{
    Patch patch;
    EngineSettings settings;
    readCurrentSound (patch, settings);

    // The tempo of the block being processed (see noteTempo), not the engine's
    // status: that only shows a block once it has been rendered.
    const double bpm = juce::jmax (1.0, stretchBpm.load());
    const TimeSignature signature { stretchTimeSigNumerator.load(), stretchTimeSigDenominator.load() };
    const double bodySeconds = FxEngine::lengthInQuarterNotes (settings.lengthUnit, settings.lengthValue, signature) * 60.0 / bpm;

    for (int slot = 0; slot < numSampleLayers; ++slot)
    {
        const auto& params = patch.samples[(size_t) slot];

        SampleData::Ptr released;
        bool changed = false;

        {
            const juce::ScopedLock scoped (slotMutex);
            auto& target = slots[(size_t) slot];

            const auto wanted = makeStretchJob (target.data, params, bodySeconds);

            if (wanted != target.wantedJob)
            {
                changed = true;
                target.wantedJob = wanted;

                // A render for another length of the same material is still
                // worth playing (a little fast or slow) until the new one is
                // ready; a render of different material is not.
                if (target.stretched != nullptr && (! wanted.isValid() || ! sameMaterial (target.stretchedJob, wanted)))
                {
                    released = std::move (target.stretched);
                    target.stretched = nullptr;
                    target.stretchedJob = {};
                    publishSources();
                }

                // Asked for while the lock is held: this runs on more than one
                // thread (the timer, and whoever restores a project), and the
                // worker must end up with the job this slot is now waiting for.
                if (wanted.isValid() && (target.stretched == nullptr || target.stretchedJob != wanted))
                    stretchWorker->request (slot, wanted);
                else
                    stretchWorker->cancel (slot);
            }
        }

        if (changed)
            ++sampleRevision;
    }
}

void FXtendAudioProcessor::stretchFinished (int slot, const StretchJob& job, SampleData::Ptr result)
{
    // Called on the stretch worker thread.
    SampleData::Ptr released;

    {
        const juce::ScopedLock scoped (slotMutex);
        auto& target = slots[(size_t) slot];

        if (target.wantedJob != job)
            return;         // settings moved on while this was rendering

        released = std::move (target.stretched);
        target.stretched = std::move (result);
        target.stretchedJob = job;
        publishSources();
    }

    ++sampleRevision;
}

bool FXtendAudioProcessor::waitForStretches (int timeoutMilliseconds)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMilliseconds);

    while (! stretchWorker->isIdle())
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;

        juce::Thread::sleep (5);
    }

    return true;
}

void FXtendAudioProcessor::timerCallback()
{
    updateStretchRequests();
}

//==============================================================================

void FXtendAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    state.removeChild (state.getChildWithName (extraStateId), nullptr);
    juce::ValueTree extra (extraStateId);
    extra.setProperty (presetNameId, getCurrentPresetName(), nullptr);

    CurveSet savedCurves;

    {
        const juce::SpinLock::ScopedLockType lock (curveLock);
        savedCurves = curves;
    }

    for (int layer = 0; layer < numSynthLayers; ++layer)
        for (int curve = 0; curve < 3; ++curve)
            extra.setProperty (curveId (layer, curve), pointsToString (savedCurves[(size_t) layer][(size_t) curve]), nullptr);

    // Samples are saved as references to the managed copies, never as audio.
    const auto refs = getSampleRefs();

    for (int slot = 0; slot < numSampleLayers; ++slot)
    {
        const auto& ref = refs[(size_t) slot];

        if (ref.isEmpty())
            continue;

        juce::ValueTree sample (sampleStateId);
        sample.setProperty (slotId, slot, nullptr);
        sample.setProperty (sampleIdId, ref.id, nullptr);
        sample.setProperty (sampleFileNameId, ref.fileName, nullptr);
        sample.setProperty (sampleNameId, ref.name, nullptr);
        sample.setProperty (samplePathId, ref.originalPath, nullptr);
        extra.appendChild (sample, nullptr);
    }

    state.appendChild (extra, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FXtendAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    const auto state = juce::ValueTree::fromXml (*xml);
    const auto extra = state.getChildWithName (extraStateId);

    Patch curvePatch;

    for (int layer = 0; layer < numSynthLayers; ++layer)
    {
        auto& synth = curvePatch.synth[(size_t) layer];
        pointsFromString (extra[curveId (layer, 0)].toString(), synth.ampPoints);
        pointsFromString (extra[curveId (layer, 1)].toString(), synth.pitchPoints);
        pointsFromString (extra[curveId (layer, 2)].toString(), synth.cutoffPoints);
    }

    std::array<SampleRef, numSampleLayers> refs;

    for (const auto& child : extra)
    {
        const int slot = child.hasType (sampleStateId) ? (int) child.getProperty (slotId, -1) : -1;

        if (juce::isPositiveAndBelow (slot, numSampleLayers))
        {
            auto& ref = refs[(size_t) slot];
            ref.id = child[sampleIdId].toString();
            ref.fileName = child[sampleFileNameId].toString();
            ref.name = child[sampleNameId].toString();
            ref.originalPath = child[samplePathId].toString();
        }
    }

    setCurves (curvePatch);
    hasUndoPatch = false;

    parameters.replaceState (state);
    restoreSamples (refs);
    setCurrentPresetName (extra[presetNameId].toString());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FXtendAudioProcessor();
}
