#pragma once

#include "FxVoice.h"
#include "MasterFx.h"
#include "Patch.h"
#include "TransportClock.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace fxtend
{

struct EngineSettings
{
    LengthUnit lengthUnit = LengthUnit::bars;
    double lengthValue = 4.0;
    RetriggerMode retrigger = RetriggerMode::restart;
    StoppedNotes stoppedNotes = StoppedNotes::full;
    double manualBpm = 120.0;
};

/** MIDI events are read from their raw bytes: building a juce::MidiMessage
    allocates memory for long messages (SysEx), which the audio thread must not do. */
inline bool isNoteOn (const juce::MidiMessageMetadata& event) noexcept
{
    // A Note On with velocity 0 is a Note Off.
    return event.numBytes == 3 && (event.data[0] & 0xf0) == 0x90 && event.data[2] > 0;
}

inline float getNoteVelocity (const juce::MidiMessageMetadata& event) noexcept
{
    return (float) event.data[2] / 127.0f;
}

inline bool isNoteOff (const juce::MidiMessageMetadata& event) noexcept
{
    if (event.numBytes != 3)
        return false;

    const int status = event.data[0] & 0xf0;
    return status == 0x80 || (status == 0x90 && event.data[2] == 0);
}

/** Identifies a note by channel and number, to match a Note Off with its Note On. */
inline int getNoteKey (const juce::MidiMessageMetadata& event) noexcept
{
    return (event.data[0] & 0x0f) * 128 + (event.data[1] & 0x7f);
}

inline bool isAllSoundOff (const juce::MidiMessageMetadata& event) noexcept
{
    return event.numBytes == 3 && (event.data[0] & 0xf0) == 0xb0 && event.data[1] == 120;
}

inline bool isAllNotesOff (const juce::MidiMessageMetadata& event) noexcept
{
    return event.numBytes == 3 && (event.data[0] & 0xf0) == 0xb0 && event.data[1] == 123;
}

/** Read by the UI thread; written by the audio thread. */
struct EngineStatus
{
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> ppq { 0.0 };
    std::atomic<int> timeSigNumerator { 4 };
    std::atomic<int> timeSigDenominator { 4 };
    std::atomic<bool> hostSynced { false };
    std::atomic<bool> tempoFromHost { false };
    std::atomic<bool> timeSigFromHost { false };
    std::atomic<double> lengthQn { 16.0 };
    std::atomic<int> activeVoices { 0 };
    std::atomic<double> phase { -1.0 };            // newest voice, -1 when silent
    std::atomic<int> lastNoteOnOffset { -1 };
    std::atomic<double> lastNoteOnPpq { 0.0 };
    std::atomic<int> noteOnCount { 0 };
    std::atomic<int> discontinuityCount { 0 };
    std::atomic<float> peakLeft { 0.0f };          // output peak of the last block
    std::atomic<float> peakRight { 0.0f };
    std::atomic<int> clipCount { 0 };              // blocks that went over full scale
    std::atomic<bool> tailActive { false };        // a Natural tail is ringing after the body

    // A short, thinned-out stretch of the newest output for the stereo display.
    // Written by the audio thread one value at a time; the display only needs
    // "recent", not "consistent", so no locking is involved.
    static constexpr int scopeSize = 512;
    static constexpr int scopeDecimation = 6;
    std::array<std::atomic<float>, scopeSize> scopeLeft {}, scopeRight {};
    int scopeWrite = 0, scopeCounter = 0;          // audio thread only
};

class FxEngine
{
public:
    static constexpr int maxOverlapVoices = 8;

    FxEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    void setSettings (const EngineSettings& newSettings) noexcept { settings = newSettings; }
    const EngineSettings& getSettings() const noexcept            { return settings; }

    /** The sound to play. Call from the audio thread (or before processing starts);
        macros are applied inside process(). */
    void setPatch (const Patch& newPatch) noexcept                { patch = newPatch; }
    const Patch& getPatch() const noexcept                        { return patch; }

    /** The audio behind the sample layers, for the next process() call only:
        the pointers are dropped again when that call returns. Without this
        call sample layers are silent for the block. */
    void setSampleSources (const SampleSources& sources) noexcept { sampleSources = sources; }

    /** Renders one block. The buffer is overwritten. Note Offs are ignored,
        except for notes played while the host is stopped in Gate mode. */
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const TransportState& transport);

    /** Thread-safe: plays the current FX at the start of the next block. */
    void triggerPreview (float velocity = 100.0f / 127.0f) noexcept;

    /** Silences everything at the start of the next block - voices, reverb and
        delay tails included (any thread). Used when the sound is replaced, so
        that the new one is never heard from the middle. A preview requested
        together with it starts after the stop. */
    void stopAll() noexcept                 { pendingStop.store (true, std::memory_order_release); }

    const EngineStatus& getStatus() const noexcept { return status; }

    /** True when no body is sounding and no tail is ringing (audio thread / offline use). */
    bool isSilent() const noexcept { return ! outputActive; }

    /** Body length in quarter notes for a length setting under a time signature. */
    static double lengthInQuarterNotes (LengthUnit unit, double value, TimeSignature timeSig) noexcept;

private:
    static constexpr int anyNote = -1, notHeld = -1;

    /** heldNote: the key that ends this voice when it is released, or notHeld. */
    void noteOn (int sampleOffset, float velocity, const BlockTiming& timing, int heldNote = notHeld) noexcept;
    void releaseHeldNotes (int noteKey, int sampleOffset) noexcept;
    void killAllVoices() noexcept;
    void silenceNow() noexcept;
    void publishStatus (const BlockTiming& timing, int activeVoices, float peakLeft, float peakRight) noexcept;
    void writeOutput (juce::AudioBuffer<float>& buffer, const float* left, const float* right) const noexcept;

    // Enough slots for the audible voices plus ones that are still fading out.
    static constexpr int numVoices = maxOverlapVoices * 2;

    std::array<FxVoice, numVoices> voices;

    // Gate mode (host stopped): the key each voice is waiting to be released
    // by, and whether such a release has ended the sound - its reverb and
    // delay then end with it instead of ringing on.
    std::array<int, numVoices> heldNotes;
    bool endedByRelease = false;
    TransportClock clock;
    EngineSettings settings;
    EngineStatus status;
    Patch patch;
    Patch effectivePatch;
    SampleSources sampleSources;
    MasterFx masterFx;
    bool outputActive = false;
    double quietSeconds = 0.0;

    juce::AudioBuffer<float> mixBuffer;     // left, right, body gate
    double sampleRate = 44100.0;
    std::uint32_t seedCounter = 0x9e3779b9u;
    FxVoice* newestVoice = nullptr;

    std::atomic<float> pendingPreviewVelocity { -1.0f };
    std::atomic<bool> pendingStop { false };
};

} // namespace fxtend
