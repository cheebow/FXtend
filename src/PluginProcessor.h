#pragma once

#include "Parameters.h"
#include "dsp/TimeStretchProcessor.h"
#include "engine/FxEngine.h"
#include "engine/OfflineRenderer.h"
#include "preset/PresetManager.h"
#include "sample/SampleManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** What the editor needs to know about one sample slot. */
struct SampleSlotInfo
{
    fxtend::SampleRef ref;
    bool loaded = false;            // audio is in memory
    bool missing = false;           // a sample is assigned but its file cannot be found
    double seconds = 0.0;           // length of the file

    bool stretchMode = false;       // the slot is in Stretch mode
    bool stretchReady = false;      // the stretched audio for the current length is ready
    bool stretchExtreme = false;    // ratio outside 0.25x .. 8x: quality not guaranteed
    double stretchRatio = 1.0;
};

class FXtendAudioProcessor final : public juce::AudioProcessor,
                                    private juce::Timer
{
public:
    /** storageRoot: where user presets and managed samples live. Leave empty
        for the standard location (tests pass a temporary folder). */
    explicit FXtendAudioProcessor (const juce::File& storageRoot = {});
    ~FXtendAudioProcessor() override;

    static juce::File getDefaultStorageRoot();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return JucePlugin_Name; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Everything below is for the editor (message thread).

    fxtend::FxEngine& getEngine() noexcept                         { return engine; }
    juce::AudioProcessorValueTreeState& getParameters() noexcept    { return parameters; }
    fxtend::PresetManager& getPresetManager() noexcept             { return presetManager; }
    fxtend::SampleManager& getSampleManager() noexcept             { return sampleManager; }

    /** The sound as it is set up right now (parameters plus custom curves). */
    fxtend::Patch getCurrentPatch() const;
    fxtend::EngineSettings getCurrentSettings() const;

    /** Replaces the whole sound (not the samples assigned to the slots). */
    void setCurrentPatch (const fxtend::Patch& patch);

    /** Everything needed to render the current FX offline at the tempo the
        engine is currently following: sound, length, tempo and sample audio. */
    fxtend::RenderRequest makeRenderRequest() const;

    /** The current sound with its sample references, as a preset. */
    fxtend::PresetData getCurrentPreset (const juce::String& name) const;

    /** Loads a complete preset, including (where they can be found) its samples. */
    void applyPreset (const fxtend::PresetData& preset);

    /** Writes the current sound and the sample files it uses into one file. */
    juce::Result exportPresetPackage (const juce::File& target) const;

    /** Loads a package made by exportPresetPackage and keeps it as a user preset. */
    juce::Result importPresetPackage (const juce::File& package);

    juce::Result loadPreset (int index);
    /** Plays a preset from its start WITHOUT loading it: the current sound
        and its parameters stay as they are. Sample layers are not part of an
        audition. It ends by itself, on stopSound(), on a MIDI note, or when
        anything is loaded or previewed. */
    juce::Result auditionPreset (int index);
    /** Index of the preset being auditioned, or -1. */
    int getAuditionIndex() const noexcept           { return auditioning.load() ? auditionIndex : -1; }
    /** The sound and name of the preset being auditioned (message thread). */
    const fxtend::Patch& getAuditionPatch() const noexcept   { return auditionShownPatch; }
    juce::String getAuditionName() const                      { return auditionName; }
    /** Silences whatever is playing, tails included, and ends an audition. */
    void stopSound() noexcept                       { endAudition(); engine.stopAll(); }
    bool isSounding() const noexcept
    {
        const auto& status = engine.getStatus();
        return status.activeVoices.load() > 0 || status.tailActive.load();
    }
    juce::Result saveUserPreset (const juce::String& name);
    juce::String getCurrentPresetName() const                       { return currentPresetName; }

    /** User presets can be renamed and deleted; factory presets cannot. */
    bool isCurrentPresetUserPreset() const;
    juce::Result renameCurrentPreset (const juce::String& newName);
    juce::Result deleteCurrentPreset();

    /** Switches FX type: a new sound from that type's starting-point layers.
        The sample slots are emptied too (another kind of FX wants other
        samples); if that discards anything, Undo brings the previous sound back. */
    void selectFxType (fxtend::FxType type);

    /** Randomises the macro knobs of the current FX type. Undo (below) takes
        back the last Random or the last FX type change that discarded samples. */
    void randomise();
    bool canUndoRandomise() const noexcept                          { return hasUndoPatch; }
    void undoRandomise();

    /** Plays the current FX once, as if a key had been pressed. */
    void triggerPreview()                                           { endAudition(); engine.triggerPreview(); }

    /** Call when the whole sound is replaced (a preset or an FX type): what was
        playing stops rather than carrying on in the middle of the new sound.
        If the host is not running and something was sounding, the new sound
        is played from its start instead, so presets can be compared by clicking. */
    void restartOrStopForNewSound();

    /** Incremented whenever the sound changes in a way parameters alone do not show. */
    int getPatchRevision() const noexcept                           { return patchRevision.load(); }

    //==========================================================================
    // Samples

    /** Imports a WAV/AIFF into the managed sample folder and assigns it to a slot.
        Also used to re-link a missing sample. */
    juce::Result loadSample (int slot, const juce::File& file);
    void clearSample (int slot);

    SampleSlotInfo getSampleInfo (int slot) const;
    fxtend::SampleData::Ptr getSampleData (int slot) const;

    /** Incremented whenever a slot's sample or its stretch state changes. */
    int getSampleRevision() const noexcept                          { return sampleRevision.load(); }

    /** Starts background stretch renders for slots whose settings, length or
        tempo changed. Called regularly by a timer; public for tests and export. */
    void updateStretchRequests();

    /** Blocks until all pending stretch renders have finished (tests, export). */
    bool waitForStretches (int timeoutMilliseconds);

private:
    using LayerCurves = std::array<fxtend::CurvePoints, 3>;        // amp, pitch, cutoff
    using CurveSet = std::array<LayerCurves, fxtend::numSynthLayers>;

    struct SampleSlot
    {
        fxtend::SampleRef ref;
        bool missing = false;
        fxtend::SampleData::Ptr data;
        fxtend::SampleData::Ptr stretched;
        fxtend::StretchJob stretchedJob;       // what `stretched` was rendered for
        fxtend::StretchJob wantedJob;          // what the current settings ask for
    };

    class StretchWorker;

    void timerCallback() override;
    fxtend::TransportState readTransport() const;
    void setCurves (const fxtend::Patch& patch);
    static void copyCurves (const CurveSet& curves, fxtend::Patch& patch) noexcept;

    void restoreSamples (const std::array<fxtend::SampleRef, fxtend::numSampleLayers>& refs);
    void installSample (int slot, const fxtend::SampleRef& ref, fxtend::SampleData::Ptr data, bool missing);
    void stretchFinished (int slot, const fxtend::StretchJob& job, fxtend::SampleData::Ptr result);
    void publishSources();
    std::array<fxtend::SampleRef, fxtend::numSampleLayers> getSampleRefs() const;

    juce::AudioProcessorValueTreeState parameters;
    ParameterReader reader;
    fxtend::PresetManager presetManager;
    fxtend::SampleManager sampleManager;
    fxtend::FxEngine engine;

    // Custom curve points are not host parameters. The message thread owns
    // `curves`; the audio thread picks up changes without ever waiting.
    CurveSet curves;
    CurveSet audioCurves;
    juce::SpinLock curveLock;
    std::atomic<bool> curvesChanged { true };

    // Sample slots, shared between the message thread and the stretch worker
    // under slotMutex. The audio thread never touches them: it reads
    // liveSources, and only if it can get sampleLock without waiting (it plays
    // the block without samples otherwise). Whoever replaces audio keeps the
    // old buffer alive until liveSources no longer points at it.
    std::array<SampleSlot, fxtend::numSampleLayers> slots;
    mutable juce::CriticalSection slotMutex;
    fxtend::SampleSources liveSources;
    juce::SpinLock sampleLock;
    std::unique_ptr<StretchWorker> stretchWorker;
    std::atomic<int> sampleRevision { 0 };

    fxtend::Patch audioPatch;

    // Audition: a preset played in place of the current sound without being
    // loaded. The message thread hands the patch over under auditionLock; the
    // audio thread takes it only if it gets the lock without waiting.
    void endAudition() noexcept;
    fxtend::Patch pendingAudition;         // guarded by auditionLock
    juce::SpinLock auditionLock;
    std::atomic<bool> auditionRequested { false }, auditionCancelled { false }, auditioning { false };
    int auditionIndex = -1;                 // message thread
    fxtend::Patch auditionShownPatch;      // message thread
    juce::String auditionName;              // message thread
    fxtend::Patch auditionPatch;           // audio thread
    bool auditionActive = false;            // audio thread
    fxtend::EngineSettings audioSettings;

    juce::String currentPresetName;
    fxtend::Patch undoPatch;
    bool hasUndoPatch = false;
    fxtend::PresetData undoPreset;     // used instead of undoPatch when undoIncludesSamples
    bool undoIncludesSamples = false;
    juce::Random random;
    std::atomic<int> patchRevision { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXtendAudioProcessor)
};
