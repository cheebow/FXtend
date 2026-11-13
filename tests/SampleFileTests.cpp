#include "PluginTestSupport.h"
#include "TestHost.h"
#include "dsp/TimeStretchProcessor.h"
#include "preset/PresetSerializer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fxtend;
using namespace fxtest;

namespace
{
    std::vector<float> channel (const SampleData& data, int index = 0)
    {
        const float* samples = data.audio.getReadPointer (index);
        return { samples, samples + data.getNumFrames() };
    }

    const SampleField& sampleField (const char* id)
    {
        for (const auto& field : getSampleLayerFields())
            if (juce::String (id) == field.id)
                return field;

        FAIL ("unknown sample field " << id);
        return getSampleLayerFields()[0];
    }
}

TEST_CASE ("Importing copies the file into the managed folder under its content hash", "[samplefile]")
{
    TempStorage storage;
    const SampleManager manager (storage.root.getChildFile ("Samples"));
    const auto source = storage.root.getChildFile ("incoming/My Riser.wav");
    writeSineFile (source, 1.0, 440.0);

    SampleRef ref;
    REQUIRE (manager.importFile (source, ref).wasOk());
    REQUIRE (ref.id.length() == 64);
    REQUIRE (ref.name == "My Riser.wav");
    REQUIRE (ref.fileName == ref.id + ".wav");
    REQUIRE (manager.getManagedFile (ref).existsAsFile());
    REQUIRE (manager.getManagedFile (ref).getParentDirectory() == manager.getDirectory());

    SECTION ("The same audio imported again, even under another name, is the same sample")
    {
        const auto copy = storage.root.getChildFile ("incoming/renamed.wav");
        REQUIRE (source.copyFileTo (copy));

        SampleRef second;
        REQUIRE (manager.importFile (copy, second).wasOk());
        REQUIRE (second.id == ref.id);
        REQUIRE (manager.getDirectory().getNumberOfChildFiles (juce::File::findFiles) == 1);
    }

    SECTION ("The managed copy still loads after the original has been moved away")
    {
        REQUIRE (source.moveFileTo (storage.root.getChildFile ("elsewhere.wav")));
        REQUIRE (manager.locate (ref));

        juce::String error;
        const auto data = manager.load (ref, error);
        REQUIRE (data != nullptr);
        REQUIRE (data->sampleRate == Catch::Approx (44100.0));
        REQUIRE (data->getNumFrames() == 44100);
        REQUIRE (data->audio.getNumChannels() == 2);
        REQUIRE (estimateFrequency (channel (*data), 0, 44100, 44100.0) == Catch::Approx (440.0).epsilon (0.002));
    }

    SECTION ("A lost managed copy is restored from the original only if that is still the same audio")
    {
        REQUIRE (manager.getManagedFile (ref).deleteFile());
        REQUIRE (manager.locate (ref));                         // original untouched: restored
        REQUIRE (manager.getManagedFile (ref).existsAsFile());

        REQUIRE (manager.getManagedFile (ref).deleteFile());
        writeSineFile (source, 1.0, 523.0);                     // same path, different sound
        REQUIRE_FALSE (manager.locate (ref));

        REQUIRE (source.deleteFile());
        REQUIRE_FALSE (manager.locate (ref));

        juce::String error;
        REQUIRE (manager.load (ref, error) == nullptr);
        REQUIRE (error.contains ("My Riser.wav"));
    }

    SECTION ("A reference from saved state cannot point outside the managed folder")
    {
        SampleRef hostile = ref;
        hostile.fileName = "../../incoming/My Riser.wav";
        REQUIRE (manager.getManagedFile (hostile).getParentDirectory() == manager.getDirectory());
    }
}

TEST_CASE ("WAV and AIFF in common formats load; other files are refused with a reason", "[samplefile]")
{
    TempStorage storage;
    const SampleManager manager (storage.root.getChildFile ("Samples"));
    SampleRef ref;
    juce::String error;

    SECTION ("Mono 16-bit WAV at 48 kHz becomes two identical channels")
    {
        const auto file = storage.root.getChildFile ("mono.wav");
        writeSineFile (file, 0.5, 220.0, 48000.0, 1, 16);
        REQUIRE (manager.importFile (file, ref).wasOk());

        const auto data = manager.load (ref, error);
        REQUIRE (data != nullptr);
        REQUIRE (data->sampleRate == Catch::Approx (48000.0));
        REQUIRE (data->audio.getNumChannels() == 2);
        REQUIRE (channel (*data, 0) == channel (*data, 1));
        REQUIRE (data->audio.getMagnitude (0, data->getNumFrames()) == Catch::Approx (0.5f).epsilon (0.01));
    }

    SECTION ("32-bit float WAV at 96 kHz")
    {
        const auto file = storage.root.getChildFile ("float.wav");
        writeSineFile (file, 0.5, 220.0, 96000.0, 2, 32);
        REQUIRE (manager.importFile (file, ref).wasOk());
        REQUIRE (manager.load (ref, error) != nullptr);
    }

    SECTION ("AIFF")
    {
        const auto file = storage.root.getChildFile ("tone.aiff");
        writeSineFile (file, 0.5, 220.0, 44100.0, 2, 24);
        REQUIRE (manager.importFile (file, ref).wasOk());
        REQUIRE (ref.fileName.endsWith (".aiff"));

        const auto data = manager.load (ref, error);
        REQUIRE (data != nullptr);
        REQUIRE (data->getNumFrames() == 22050);
    }

    SECTION ("Longer than 60 seconds")
    {
        const auto file = storage.root.getChildFile ("long.wav");
        writeSineFile (file, 61.0, 100.0, 8000.0, 1, 16);
        const auto result = manager.importFile (file, ref);
        REQUIRE (result.failed());
        REQUIRE (result.getErrorMessage().contains ("60"));
        REQUIRE (ref.isEmpty());
    }

    SECTION ("Not audio, wrong type, or not there")
    {
        const auto fake = storage.root.getChildFile ("fake.wav");
        fake.replaceWithText ("this is not a wave file");
        REQUIRE (manager.importFile (fake, ref).failed());

        const auto text = storage.root.getChildFile ("notes.txt");
        text.replaceWithText ("hello");
        REQUIRE (manager.importFile (text, ref).failed());

        REQUIRE (manager.importFile (storage.root.getChildFile ("nowhere.wav"), ref).failed());
        REQUIRE (ref.isEmpty());
    }
}

TEST_CASE ("Time stretch changes the length exactly and keeps the pitch", "[samplefile][stretch]")
{
    StretchJob job;
    job.source = makeSineSample (3.0, 440.0);
    job.targetSeconds = 7.5;

    REQUIRE (job.getSourceSeconds() == Catch::Approx (3.0));
    REQUIRE (job.getRatio() == Catch::Approx (2.5));
    REQUIRE_FALSE (job.isExtreme());

    SECTION ("3 s -> 7.5 s (4 bars at 128 BPM)")
    {
        const auto result = stretchSample (job);
        REQUIRE (result != nullptr);
        REQUIRE (result->getNumFrames() == 330750);             // 7.5 s at 44.1 kHz
        REQUIRE (result->sampleRate == Catch::Approx (44100.0));

        const auto audio = channel (*result);
        REQUIRE (estimateFrequency (audio, 44100, 290000, 44100.0) == Catch::Approx (440.0).epsilon (0.005));

        // Sounding the whole way through at about the original level.
        for (int from = 22050; from < 330750 - 22050; from += 22050)
            REQUIRE (result->audio.getMagnitude (0, from, 22050) == Catch::Approx (0.5f).epsilon (0.15));
    }

    SECTION ("Shortening works too")
    {
        job.targetSeconds = 1.0;
        const auto result = stretchSample (job);
        REQUIRE (result != nullptr);
        REQUIRE (result->getNumFrames() == 44100);
        REQUIRE (estimateFrequency (channel (*result), 8000, 36000, 44100.0) == Catch::Approx (440.0).epsilon (0.005));
    }

    SECTION ("Pitch transposes without changing the length")
    {
        job.pitchSemitones = 12.0f;
        const auto result = stretchSample (job);
        REQUIRE (result != nullptr);
        REQUIRE (result->getNumFrames() == 330750);
        REQUIRE (estimateFrequency (channel (*result), 44100, 290000, 44100.0) == Catch::Approx (880.0).epsilon (0.005));
    }

    SECTION ("Trim and reverse choose the material")
    {
        job.source = makeSineSample (2.0, 440.0, 44100.0, 0.0f, 1.0f);     // gets louder
        job.trimStart = 0.5f;
        job.reverse = true;
        job.targetSeconds = 2.0;
        REQUIRE (job.getSourceSeconds() == Catch::Approx (1.0));

        const auto result = stretchSample (job);
        REQUIRE (result != nullptr);

        // The loud second half, backwards: starts near full level and falls to about half.
        REQUIRE (result->audio.getMagnitude (0, 4410, 8820) > 0.85f);
        REQUIRE (result->audio.getMagnitude (0, 75000, 8820) < 0.65f);
        REQUIRE (result->audio.getMagnitude (0, 75000, 8820) > 0.4f);
    }

    SECTION ("Extreme ratios are flagged; an abort or a pointless job gives nothing")
    {
        job.targetSeconds = 30.0;
        REQUIRE (job.isExtreme());
        job.targetSeconds = 0.5;
        REQUIRE (job.isExtreme());

        job.targetSeconds = 7.5;
        REQUIRE (stretchSample (job, [] { return true; }) == nullptr);

        job.source = nullptr;
        REQUIRE (stretchSample (job) == nullptr);
    }
}

TEST_CASE ("A 3 s WAV in Stretch mode fills 4 bars at 128 BPM at its original pitch", "[samplefile][plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    FXtendAudioProcessor processor (storage.root);
    FakePlayHead playHead;

    // Only the sample should sound.
    Patch patch;
    patch.samples[0].levelDb = 0.0f;
    patch.master.limiter = 0.0f;
    processor.setCurrentPatch (patch);
    setParameter (processor, ParamIDs::lengthValue, 4.0f);

    const auto file = storage.root.getChildFile ("three seconds.wav");
    writeSineFile (file, 3.0, 440.0);
    REQUIRE (processor.loadSample (0, file).wasOk());

    // Let the plugin see the host tempo, then give the background stretch time to finish.
    playNote (processor, playHead, 512, 4096, 48000.0, false);
    processor.updateStretchRequests();
    REQUIRE (processor.waitForStretches (60000));

    const auto info = processor.getSampleInfo (0);
    REQUIRE (info.loaded);
    REQUIRE (info.stretchMode);
    REQUIRE (info.stretchReady);
    REQUIRE (info.stretchRatio == Catch::Approx (2.5));
    REQUIRE_FALSE (info.stretchExtreme);

    const auto playback = playNote (processor, playHead, 512, 9 * 48000);

    REQUIRE (playback.lastAudible < 360000);                    // 7.5 s
    REQUIRE (playback.lastAudible > 360000 - 960);
    REQUIRE (estimateFrequency (playback.left, 48000, 320000, 48000.0) == Catch::Approx (440.0).epsilon (0.005));

    SECTION ("Changing the tempo re-renders for the new length: 4 bars at 100 BPM = 9.6 s, still 440 Hz")
    {
        playHead.bpm = 100.0;
        playNote (processor, playHead, 512, 4096, 48000.0, false);
        processor.updateStretchRequests();
        REQUIRE_FALSE (processor.getSampleInfo (0).stretchReady);
        REQUIRE (processor.waitForStretches (60000));
        REQUIRE (processor.getSampleInfo (0).stretchReady);

        const auto slower = playNote (processor, playHead, 512, 11 * 48000);
        REQUIRE (slower.lastAudible < 460800);
        REQUIRE (slower.lastAudible > 460800 - 960);
        REQUIRE (estimateFrequency (slower.left, 48000, 400000, 48000.0) == Catch::Approx (440.0).epsilon (0.005));
    }

    SECTION ("Resample mode needs no stretch and changes the pitch instead")
    {
        setParameter (processor, ParamIDs::sampleLayer (0, sampleField ("mode")), (float) (int) SampleMode::resample);
        processor.updateStretchRequests();
        REQUIRE_FALSE (processor.getSampleInfo (0).stretchMode);

        const auto resampled = playNote (processor, playHead, 512, 9 * 48000);
        REQUIRE (resampled.lastAudible < 360000);
        REQUIRE (estimateFrequency (resampled.left, 48000, 320000, 48000.0) == Catch::Approx (440.0 * 3.0 / 7.5).epsilon (0.005));
    }
}

TEST_CASE ("Projects find their samples again; a lost sample is reported, never replaced", "[samplefile][plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    const auto file = storage.root.getChildFile ("user files/hit.wav");
    writeSineFile (file, 1.0, 330.0);

    juce::MemoryBlock saved;
    SampleRef savedRef;

    {
        FXtendAudioProcessor processor (storage.root);
        REQUIRE (processor.loadSample (2, file).wasOk());
        setParameter (processor, ParamIDs::sampleLayer (2, sampleField ("mode")), (float) (int) SampleMode::oneShot);
        setParameter (processor, ParamIDs::sampleLayer (2, sampleField ("trimEnd")), 0.75f);

        savedRef = processor.getSampleInfo (2).ref;
        processor.getStateInformation (saved);

        // The audio itself is not in the saved state, only a reference to it.
        REQUIRE (saved.getSize() < 100 * 1024);
    }

    SECTION ("After the source file was moved, the managed copy is used")
    {
        REQUIRE (file.moveFileTo (storage.root.getChildFile ("moved.wav")));

        FXtendAudioProcessor restored (storage.root);
        restored.setStateInformation (saved.getData(), (int) saved.getSize());

        const auto info = restored.getSampleInfo (2);
        REQUIRE (info.loaded);
        REQUIRE_FALSE (info.missing);
        REQUIRE (info.ref.id == savedRef.id);
        REQUIRE (info.ref.name == "hit.wav");
        REQUIRE (info.seconds == Catch::Approx (1.0));
        REQUIRE (restored.getCurrentPatch().samples[2].getMode() == SampleMode::oneShot);
        REQUIRE (restored.getCurrentPatch().samples[2].trimEnd == Catch::Approx (0.75f));
        REQUIRE_FALSE (restored.getSampleInfo (0).loaded);
    }

    SECTION ("With no copy anywhere the slot says Missing, the synth still plays, and re-linking fixes it")
    {
        REQUIRE (file.deleteFile());
        REQUIRE (storage.root.getChildFile ("Samples").deleteRecursively());

        FXtendAudioProcessor restored (storage.root);
        restored.setStateInformation (saved.getData(), (int) saved.getSize());

        auto info = restored.getSampleInfo (2);
        REQUIRE (info.missing);
        REQUIRE_FALSE (info.loaded);
        REQUIRE (info.ref.id == savedRef.id);       // the reference is kept, not dropped
        REQUIRE (info.ref.name == "hit.wav");

        // Saving again must not lose the reference either.
        juce::MemoryBlock resaved;
        restored.getStateInformation (resaved);
        FXtendAudioProcessor again (storage.root);
        again.setStateInformation (resaved.getData(), (int) resaved.getSize());
        REQUIRE (again.getSampleInfo (2).missing);
        REQUIRE (again.getSampleInfo (2).ref.id == savedRef.id);

        FakePlayHead playHead;
        setParameter (restored, ParamIDs::tailMode, 0.0f);
        setParameter (restored, ParamIDs::lengthValue, 1.0f);
        const auto playback = playNote (restored, playHead, 512, 2 * 48000);
        REQUIRE (playback.lastAudible > 80000);      // the Riser preset's synth layers
        REQUIRE (playback.lastAudible < 90000);

        const auto replacement = storage.root.getChildFile ("found again.wav");
        writeSineFile (replacement, 1.0, 330.0);
        REQUIRE (restored.loadSample (2, replacement).wasOk());

        info = restored.getSampleInfo (2);
        REQUIRE (info.loaded);
        REQUIRE_FALSE (info.missing);
        REQUIRE (info.ref.id == savedRef.id);       // same audio, same identity
    }

    SECTION ("Clearing a slot empties it")
    {
        FXtendAudioProcessor restored (storage.root);
        restored.setStateInformation (saved.getData(), (int) saved.getSize());
        restored.clearSample (2);

        const auto info = restored.getSampleInfo (2);
        REQUIRE_FALSE (info.loaded);
        REQUIRE_FALSE (info.missing);
        REQUIRE (info.ref.isEmpty());
    }
}

TEST_CASE ("User presets remember their samples by reference", "[samplefile][plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    const auto file = storage.root.getChildFile ("clap layer.aif");
    writeSineFile (file, 0.5, 600.0);

    FXtendAudioProcessor processor (storage.root);
    REQUIRE (processor.loadSample (1, file).wasOk());
    setParameter (processor, ParamIDs::sampleLayer (1, sampleField ("reverse")), 1.0f);
    REQUIRE (processor.saveUserPreset ("With Sample").wasOk());

    // The preset file holds the reference and the layer settings, not the audio.
    const auto presetFile = storage.root.getChildFile ("Presets/With Sample.fxpreset");
    REQUIRE (presetFile.existsAsFile());
    REQUIRE (presetFile.getSize() < 64 * 1024);

    PresetData onDisk;
    REQUIRE (PresetSerializer::fromJson (presetFile.loadFileAsString(), onDisk).wasOk());
    REQUIRE (onDisk.usesSamples());
    REQUIRE (onDisk.sampleRefs[1].name == "clap layer.aif");
    REQUIRE (onDisk.sampleRefs[0].isEmpty());
    REQUIRE (onDisk.patch.samples[1].isReversed());

    // A factory preset has no samples: the slot empties. Coming back restores it.
    REQUIRE (processor.loadPreset (0).wasOk());
    REQUIRE_FALSE (processor.getSampleInfo (1).loaded);

    REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("With Sample")).wasOk());
    REQUIRE (processor.getSampleInfo (1).loaded);
    REQUIRE (processor.getSampleInfo (1).ref.name == "clap layer.aif");
    REQUIRE (processor.getCurrentPatch().samples[1].isReversed());

    // Choosing another FX type starts a new sound: the slots empty and the
    // sample settings go back to their defaults...
    REQUIRE_FALSE (processor.canUndoRandomise());
    processor.selectFxType (FxType::impact);
    REQUIRE (processor.getCurrentPatch().fxType == FxType::impact);
    REQUIRE_FALSE (processor.getSampleInfo (1).loaded);
    REQUIRE (processor.getSampleInfo (1).ref.isEmpty());
    REQUIRE_FALSE (processor.getCurrentPatch().samples[1].isReversed());

    // ...and Undo brings the previous sound back, samples included.
    REQUIRE (processor.canUndoRandomise());
    processor.undoRandomise();
    REQUIRE_FALSE (processor.canUndoRandomise());
    REQUIRE (processor.getCurrentPresetName() == "With Sample");
    REQUIRE (processor.getSampleInfo (1).loaded);
    REQUIRE (processor.getSampleInfo (1).ref.name == "clap layer.aif");
    REQUIRE (processor.getCurrentPatch().samples[1].isReversed());
}

TEST_CASE ("Loading into a slot that already has a sample replaces it; a slot can be switched off", "[samplefile][plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    FXtendAudioProcessor processor (storage.root);
    processor.prepareToPlay (48000.0, 512);

    const auto first = storage.root.getChildFile ("First.wav");
    const auto second = storage.root.getChildFile ("Second.wav");
    writeSineFile (first, 1.0, 220.0);
    writeSineFile (second, 2.0, 880.0);

    REQUIRE (processor.loadSample (0, first).wasOk());
    REQUIRE (processor.getSampleInfo (0).ref.name == "First.wav");

    REQUIRE (processor.loadSample (0, second).wasOk());
    const auto info = processor.getSampleInfo (0);
    REQUIRE (info.loaded);
    REQUIRE (info.ref.name == "Second.wav");
    REQUIRE (info.seconds == Catch::Approx (2.0).margin (0.01));
    REQUIRE_FALSE (processor.getSampleInfo (1).loaded);

    // Switched off, the slot keeps its sample but is silent.
    for (auto& layer : { 0, 1, 2, 3 })
        setParameter (processor, ParamIDs::synthLayer (layer, getSynthLayerFields()[0]), 0.0f);

    setParameter (processor, ParamIDs::sampleLayer (0, sampleField ("mode")), 1.0f);    // Resample: no stretch render to wait for
    setParameter (processor, ParamIDs::lengthValue, 1.0f);

    FakePlayHead playHead;
    REQUIRE (playOneNote (processor, playHead, 512, 2 * 48000) > 1000);

    setParameter (processor, ParamIDs::sampleLayer (0, sampleField ("on")), 0.0f);
    REQUIRE (processor.getSampleInfo (0).loaded);
    REQUIRE (playOneNote (processor, playHead, 512, 2 * 48000) < 0);
}
