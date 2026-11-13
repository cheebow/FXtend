#include "PluginTestSupport.h"
#include "TestHost.h"
#include "engine/FxTemplates.h"
#include "preset/FactoryPresets.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>

// ---------------------------------------------------------------------------
// Counts heap allocations made by the current thread while `watching` is set.
// Replacing operator new for the whole test program is the simplest way to
// catch an allocation anywhere below the audio callback.
// ---------------------------------------------------------------------------
namespace
{
    thread_local bool watching = false;
    std::atomic<int> watchedAllocations { 0 };

    void* allocate (std::size_t size)
    {
        if (watching)
            ++watchedAllocations;

        if (void* memory = std::malloc (size > 0 ? size : 1))
            return memory;

        throw std::bad_alloc();
    }

    void* allocateAligned (std::size_t size, std::align_val_t alignment)
    {
        if (watching)
            ++watchedAllocations;

        void* memory = nullptr;

        if (posix_memalign (&memory, std::max (sizeof (void*), (std::size_t) alignment), size > 0 ? size : 1) != 0)
            throw std::bad_alloc();

        return memory;
    }

    struct AllocationWatch
    {
        AllocationWatch()  { watchedAllocations = 0; watching = true; }
        ~AllocationWatch() { watching = false; }
        static int count() { return watchedAllocations.load(); }
    };
}

void* operator new (std::size_t size)                                   { return allocate (size); }
void* operator new[] (std::size_t size)                                 { return allocate (size); }
void* operator new (std::size_t size, std::align_val_t alignment)       { return allocateAligned (size, alignment); }
void* operator new[] (std::size_t size, std::align_val_t alignment)     { return allocateAligned (size, alignment); }
void operator delete (void* memory) noexcept                            { std::free (memory); }
void operator delete[] (void* memory) noexcept                          { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept               { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept             { std::free (memory); }
void operator delete (void* memory, std::align_val_t) noexcept          { std::free (memory); }
void operator delete[] (void* memory, std::align_val_t) noexcept        { std::free (memory); }
void operator delete (void* memory, std::size_t, std::align_val_t) noexcept    { std::free (memory); }
void operator delete[] (void* memory, std::size_t, std::align_val_t) noexcept  { std::free (memory); }

using namespace fxtend;
using namespace fxtest;

namespace
{
    /** A demanding patch: every synth layer, four sample layers, delay and reverb. */
    Patch heavyPatch()
    {
        Patch patch = makeTemplate (FxType::impact);
        patch.tailMode = TailMode::natural;
        patch.master.delayMix = 0.3f;
        patch.master.reverbMix = 0.3f;
        patch.master.highPassHz = 40.0f;
        patch.master.lowPassHz = 16000.0f;
        patch.master.tiltDb = 1.5f;
        patch.master.drive = 0.2f;

        for (size_t i = 0; i < patch.samples.size(); ++i)
        {
            patch.samples[i].enabled = 1.0f;
            patch.samples[i].mode = (float) i;      // one of each mode
            patch.samples[i].levelDb = -12.0f;
        }

        return patch;
    }
}

TEST_CASE ("The allocation watch used by these tests does notice allocations", "[realtime]")
{
    std::vector<float> buffer;
    volatile std::size_t size = 4096;

    {
        const AllocationWatch watch;
        buffer.resize (size);
    }

    REQUIRE (AllocationWatch::count() >= 1);

    {
        const AllocationWatch watch;
        buffer[0] = 1.0f;           // no allocation here
    }

    REQUIRE (AllocationWatch::count() == 0);
}

TEST_CASE ("The engine never allocates memory while processing", "[realtime]")
{
    const auto sample = makeSineSample (2.0, 220.0);
    const auto stretched = makeSineSample (1.875, 220.0);

    FxEngine engine;
    EngineSettings settings;
    settings.lengthValue = 1.0;
    settings.retrigger = RetriggerMode::overlap;
    engine.setSettings (settings);
    engine.setPatch (heavyPatch());
    engine.prepare (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi, noMidi;
    midi.ensureSize (256);
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 17);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 200);

    SampleSources sources;

    for (auto& source : sources)
        source = { sample.get(), stretched.get() };

    const auto& presets = getFactoryPresets();
    TransportState transport;
    transport.isPlaying = true;
    transport.hasPpq = transport.hasBpm = transport.hasTimeSig = true;
    transport.bpm = 128.0;

    int allocations = 0;

    for (int block = 0; block < 1500; ++block)
    {
        // Everything a host and a user can throw at it: retriggers, tempo
        // changes, loop jumps, stop/start, preset changes, samples coming and going.
        transport.bpm = block > 600 ? 97.5 : 128.0;
        transport.isPlaying = block < 1000 || block > 1040;
        transport.ppq = (block == 400 ? 0.0 : transport.ppq) + 512 * transport.bpm / (60.0 * 48000.0);

        if (block % 200 == 150)
        {
            Patch next = presets[(size_t) (block / 200) % presets.size()].patch;
            next.samples = heavyPatch().samples;
            engine.setPatch (next);
        }

        const bool withSamples = block % 300 < 250;

        const AllocationWatch watch;
        engine.setSampleSources (withSamples ? sources : SampleSources {});
        engine.process (buffer, block % 12 == 0 ? midi : noMidi, transport);
        allocations += AllocationWatch::count();
    }

    REQUIRE (allocations == 0);
}

TEST_CASE ("The plugin's audio callback never allocates memory", "[realtime][plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    FXtendAudioProcessor processor (storage.root);
    FakePlayHead playHead;

    const auto file = storage.root.getChildFile ("layer.wav");
    writeSineFile (file, 2.0, 220.0);
    REQUIRE (processor.loadSample (0, file).wasOk());
    REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("Impact 01 Deep")).wasOk());
    REQUIRE (processor.loadSample (1, file).wasOk());

    processor.setPlayHead (&playHead);
    processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (256);

    int allocations = 0;

    for (int block = 0; block < 2000; ++block)
    {
        midi.clear();

        if (block % 40 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 48 + block % 12, (juce::uint8) 110), block % 256);

        // Meanwhile the "user" turns knobs and swaps samples on the message thread.
        if (block % 97 == 0)
            setParameter (processor, ParamIDs::macro (block % numMacros), (float) (block % 10) / 10.0f);

        if (block == 700)
            processor.clearSample (1);

        if (block == 900)
            REQUIRE (processor.loadSample (2, file).wasOk());

        if (block % 50 == 0)
            processor.updateStretchRequests();

        {
            const AllocationWatch watch;
            processor.processBlock (buffer, midi);
            allocations += AllocationWatch::count();
        }

        playHead.ppq += 256 * playHead.bpm / (60.0 * 48000.0);
    }

    processor.setPlayHead (nullptr);
    REQUIRE (processor.waitForStretches (60000));
    REQUIRE (allocations == 0);
}

TEST_CASE ("Eight overlapping voices of a heavy patch leave plenty of CPU headroom", "[realtime][performance]")
{
    const auto sample = makeSineSample (2.0, 220.0);
    const auto stretched = makeSineSample (1.875, 220.0);

    HostSim host;
    host.blockSize = 128;

    for (int i = 0; i < 40; ++i)
        host.events.push_back ({ i * 9000, true });         // a new hit every 187 ms: 8+ voices at once

    for (auto& source : host.sources)
        source = { sample.get(), stretched.get() };

    FxEngine engine;
    EngineSettings settings;
    settings.lengthValue = 1.0;
    settings.retrigger = RetriggerMode::overlap;
    engine.setSettings (settings);
    engine.setPatch (heavyPatch());
    engine.prepare (host.sampleRate, host.blockSize);

    const long long totalSamples = 10 * 48000;
    const auto start = std::chrono::steady_clock::now();
    const auto audio = host.render (engine, totalSamples);
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    const double timesRealTime = ((double) totalSamples / 48000.0) / seconds;
    WARN ("8 overlapping voices, 4 synth + 4 sample layers each, with master FX: " << (int) timesRealTime << "x real time");

    REQUIRE (fxtest::peakBetween (audio, 48000, 96000) > 0.05f);

   #if defined (NDEBUG)
    REQUIRE (timesRealTime > 4.0);
   #endif
}
