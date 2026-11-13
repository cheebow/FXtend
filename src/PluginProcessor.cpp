#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "engine/FxTemplates.h"
#include "preset/PresetPackage.h"

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

            auto result = stretchSample (job, [&] { return threadShouldExit() || superseded(); });

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
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("FXtend");
}

FXtendAudioProcessor::FXtendAudioProcessor (const juce::File& storageRoot)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "FXtend", createParameterLayout()),
      reader (parameters),
      presetManager ((storageRoot == juce::File() ? getDefaultStorageRoot() : storageRoot).getChildFile ("Presets")),
      sampleManager ((storageRoot == juce::File() ? getDefaultStorageRoot() : storageRoot).getChildFile ("Samples"))
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
    engine.setSettings (getCurrentSettings());
    engine.setPatch (getCurrentPatch());
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
    return getCurrentPatch().tailMode == TailMode::natural ? 8.0 : 0.0;
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

    // ---- Audition: a preset heard without being loaded ---------------------
    bool endAuditionNow = auditionCancelled.exchange (false, std::memory_order_acquire);

    if (auditionActive && ! endAuditionNow)
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOn())      // a played note always means the real sound
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
            engine.stopAll();               // handled before the preview, inside process()
            engine.triggerPreview();
        }
    }

    engine.setSettings (audioSettings);
    engine.setPatch (auditionActive ? auditionPatch : audioPatch);

    {
        // Held for the whole block so the sample audio cannot be released
        // underneath the engine. If a sample is being swapped right now, this
        // one block plays without the sample layers instead of waiting.
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);

        if (lock.isLocked())
            engine.setSampleSources (liveSources);

        engine.process (buffer, midi, readTransport());
    }

    if (auditionActive && ! auditionStartsNow && engine.isSilent())
        auditionActive = false;             // it has rung out

    if (! auditionActive && ! auditionRequested.load (std::memory_order_acquire))
        auditioning.store (false, std::memory_order_release);

    midi.clear();
}

juce::AudioProcessorEditor* FXtendAudioProcessor::createEditor()
{
    return new FXtendAudioProcessorEditor (*this);
}

//==============================================================================

Patch FXtendAudioProcessor::getCurrentPatch() const
{
    Patch patch;
    EngineSettings settings;
    reader.read (patch, settings);
    copyCurves (curves, patch);
    return patch;
}

EngineSettings FXtendAudioProcessor::getCurrentSettings() const
{
    Patch patch;
    EngineSettings settings;
    reader.read (patch, settings);
    return settings;
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
    request.patch = getCurrentPatch();
    request.settings = getCurrentSettings();
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

void FXtendAudioProcessor::restartOrStopForNewSound()
{
    const auto& status = engine.getStatus();
    const bool wasAuditioning = auditioning.load (std::memory_order_acquire);
    const bool wasSounding = ! wasAuditioning && (status.activeVoices.load() > 0 || status.tailActive.load());
    const bool hostRunning = status.hostSynced.load();

    endAudition();      // an audition is only stopped, never turned into a preview

    engine.stopAll();

    // With the host running, a restart would land off the grid: just stop.
    if (wasSounding && ! hostRunning)
        engine.triggerPreview();
}

void FXtendAudioProcessor::applyPreset (const PresetData& preset)
{
    restartOrStopForNewSound();
    setCurrentPatch (preset.patch);
    restoreSamples (preset.sampleRefs);
    currentPresetName = preset.name;
    hasUndoPatch = false;
    ++patchRevision;
}

juce::Result FXtendAudioProcessor::exportPresetPackage (const juce::File& target) const
{
    auto name = currentPresetName.endsWith ("(init)") ? juce::String() : currentPresetName;

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

    // A factory preset cannot be overwritten, so an import with such a name is kept beside it.
    if (presetManager.isFactory (presetManager.indexOf (preset.name)))
        preset.name << " (imported)";

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
    {
        currentPresetName = name.trim();
        ++patchRevision;
    }

    return result;
}

bool FXtendAudioProcessor::isCurrentPresetUserPreset() const
{
    const int index = presetManager.indexOf (currentPresetName);
    return index >= 0 && ! presetManager.isFactory (index);
}

juce::Result FXtendAudioProcessor::renameCurrentPreset (const juce::String& newName)
{
    const auto result = presetManager.renameUser (presetManager.indexOf (currentPresetName), newName);

    if (result.wasOk())
    {
        currentPresetName = newName.trim();
        ++patchRevision;
    }

    return result;
}

juce::Result FXtendAudioProcessor::deleteCurrentPreset()
{
    const auto result = presetManager.deleteUser (presetManager.indexOf (currentPresetName));

    if (result.wasOk())
    {
        // The sound stays as it is; it just no longer belongs to a saved preset.
        currentPresetName = {};
        ++patchRevision;
    }

    return result;
}

void FXtendAudioProcessor::selectFxType (FxType type)
{
    const auto previous = getCurrentPreset (currentPresetName);

    bool hadSamples = false;

    for (const auto& ref : previous.sampleRefs)
        hadSamples = hadSamples || ! ref.isEmpty();

    Patch patch = makeTemplate (type);
    patch.masterGainDb = previous.patch.masterGainDb;

    restartOrStopForNewSound();

    setCurrentPatch (patch);
    restoreSamples ({});                        // another kind of FX wants other samples
    currentPresetName = juce::String (getFxTypeName (type)) + " (init)";

    // Losing loaded samples to one click must be reversible.
    hasUndoPatch = hadSamples;
    undoIncludesSamples = hadSamples;

    if (hadSamples)
        undoPreset = previous;

    ++patchRevision;
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
    }

    stretchWorker->cancel (slot);
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
    return juce::Result::ok();
}

void FXtendAudioProcessor::clearSample (int slot)
{
    if (juce::isPositiveAndBelow (slot, numSampleLayers))
        installSample (slot, {}, nullptr, false);
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
    const Patch patch = getCurrentPatch();
    const EngineSettings settings = getCurrentSettings();
    const auto& status = engine.getStatus();

    const double bpm = juce::jmax (1.0, status.bpm.load());
    const TimeSignature signature { status.timeSigNumerator.load(), status.timeSigDenominator.load() };
    const double bodySeconds = FxEngine::lengthInQuarterNotes (settings.lengthUnit, settings.lengthValue, signature) * 60.0 / bpm;

    for (int slot = 0; slot < numSampleLayers; ++slot)
    {
        const auto& params = patch.samples[(size_t) slot];

        SampleData::Ptr released;
        bool changed = false, submit = false;
        StretchJob wanted;

        {
            const juce::ScopedLock scoped (slotMutex);
            auto& target = slots[(size_t) slot];

            wanted = makeStretchJob (target.data, params, bodySeconds);

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

                submit = wanted.isValid() && (target.stretched == nullptr || target.stretchedJob != wanted);
            }
        }

        if (changed)
        {
            if (submit)
                stretchWorker->request (slot, wanted);
            else
                stretchWorker->cancel (slot);

            ++sampleRevision;
        }
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
    extra.setProperty (presetNameId, currentPresetName, nullptr);

    for (int layer = 0; layer < numSynthLayers; ++layer)
        for (int curve = 0; curve < 3; ++curve)
            extra.setProperty (curveId (layer, curve), pointsToString (curves[(size_t) layer][(size_t) curve]), nullptr);

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
    currentPresetName = extra[presetNameId].toString();
    hasUndoPatch = false;

    parameters.replaceState (state);
    restoreSamples (refs);
    ++patchRevision;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FXtendAudioProcessor();
}
