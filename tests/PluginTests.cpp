#include "PluginEditor.h"
#include "PluginTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>

#include <cstdlib>

#include <CoreFoundation/CoreFoundation.h>

using namespace fxtend;

using namespace fxtest;

TEST_CASE ("The plugin ends its body on the bar line reported by the host", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    FXtendAudioProcessor processor (storage.root);
    FakePlayHead playHead;

    setParameter (processor, ParamIDs::tailMode, 0.0f);     // Strict
    setParameter (processor, ParamIDs::lengthUnit, 0.0f);
    setParameter (processor, ParamIDs::lengthValue, 4.0f);

    SECTION ("128 BPM: 4 bars = 7.5 s")
    {
        const long long last = playOneNote (processor, playHead, 512, 9 * 48000);
        REQUIRE (last < 360000);
        REQUIRE (last > 360000 - 480);
    }

    SECTION ("100 BPM: 4 bars = 9.6 s")
    {
        playHead.bpm = 100.0;
        const long long last = playOneNote (processor, playHead, 256, 11 * 48000);
        REQUIRE (last < 460800);
        REQUIRE (last > 460800 - 480);
    }

    SECTION ("1 beat at 96 kHz")
    {
        setParameter (processor, ParamIDs::lengthUnit, 1.0f);
        setParameter (processor, ParamIDs::lengthValue, 1.0f);
        const long long last = playOneNote (processor, playHead, 64, 96000, 96000.0);
        REQUIRE (last < 45000);
        REQUIRE (last > 45000 - 960);
    }
}

TEST_CASE ("Saving and restoring the plugin state brings back the same sound", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    TempStorage storage;
    juce::MemoryBlock saved;
    Patch original;
    EngineSettings originalSettings;

    {
        FXtendAudioProcessor processor (storage.root);
        REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("Whoosh 02 Wide Resonant")).wasOk());
        setParameter (processor, ParamIDs::lengthValue, 8.0f);
        setParameter (processor, ParamIDs::macro (1), 0.9f);

        original = processor.getCurrentPatch();
        originalSettings = processor.getCurrentSettings();
        processor.getStateInformation (saved);
    }

    FXtendAudioProcessor restored (storage.root);
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    const Patch patch = restored.getCurrentPatch();
    REQUIRE (restored.getCurrentPresetName() == "Whoosh 02 Wide Resonant");
    REQUIRE (patch.fxType == original.fxType);
    REQUIRE (patch.tailMode == original.tailMode);
    REQUIRE (patch.macros[1] == Catch::Approx (0.9f));
    REQUIRE (restored.getCurrentSettings().lengthValue == Catch::Approx (8.0));

    for (size_t layer = 0; layer < patch.synth.size(); ++layer)
    {
        for (const auto& field : getSynthLayerFields())
        {
            INFO ("layer " << layer << " " << field.id);
            REQUIRE (patch.synth[layer].*(field.member) == Catch::Approx (original.synth[layer].*(field.member)).epsilon (1.0e-4).margin (1.0e-4));
        }

        // Custom curve points are not host parameters but must come back too.
        REQUIRE (patch.synth[layer].ampPoints == original.synth[layer].ampPoints);
    }

    // Garbage must not crash or change anything.
    const char junk[] = "this is not plugin state";
    restored.setStateInformation (junk, (int) sizeof (junk));
    REQUIRE (restored.getCurrentPatch().fxType == original.fxType);
}

TEST_CASE ("Choosing an FX type loads its layers; Random can be undone", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    FXtendAudioProcessor processor (storage.root);

    processor.selectFxType (FxType::clap);
    REQUIRE (processor.getCurrentPatch().fxType == FxType::clap);
    REQUIRE (processor.getCurrentPatch().synth[0].getType() == SynthType::clap);
    REQUIRE (processor.getCurrentPatch().tailMode == TailMode::natural);

    const auto before = processor.getCurrentPatch().macros;
    REQUIRE_FALSE (processor.canUndoRandomise());

    processor.randomise();
    REQUIRE (processor.getCurrentPatch().fxType == FxType::clap);
    REQUIRE (processor.getCurrentPatch().macros != before);
    REQUIRE (processor.canUndoRandomise());

    processor.undoRandomise();

    for (size_t i = 0; i < before.size(); ++i)
        REQUIRE (processor.getCurrentPatch().macros[i] == Catch::Approx (before[i]));
}

TEST_CASE ("The current user preset can be renamed; factory presets cannot", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    FXtendAudioProcessor processor (storage.root);

    REQUIRE_FALSE (processor.isCurrentPresetUserPreset());
    REQUIRE (processor.renameCurrentPreset ("Nope").failed());
    REQUIRE (processor.deleteCurrentPreset().failed());

    REQUIRE (processor.saveUserPreset ("First Name").wasOk());
    REQUIRE (processor.isCurrentPresetUserPreset());

    REQUIRE (processor.renameCurrentPreset ("Second Name").wasOk());
    REQUIRE (processor.getCurrentPresetName() == "Second Name");
    REQUIRE (processor.getPresetManager().indexOf ("First Name") == -1);
    REQUIRE (storage.root.getChildFile ("Presets/Second Name.fxpreset").existsAsFile());

    // The renamed preset loads back as the same sound.
    const auto before = processor.getCurrentPatch();
    REQUIRE (processor.loadPreset (0).wasOk());
    REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("Second Name")).wasOk());
    REQUIRE (processor.getCurrentPatch().fxType == before.fxType);
}

// Saves a picture of the editor for checking the layout:
//   FXTEND_RENDER_DIR=/some/folder FXtendTests "[.snapshot]"
TEST_CASE ("Render the editor to PNG files", "[.snapshot]")
{
    const char* directory = std::getenv ("FXTEND_RENDER_DIR");
    REQUIRE (directory != nullptr);

    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    FXtendAudioProcessor processor (storage.root);
    processor.prepareToPlay (48000.0, 512);

    const auto sample = storage.root.getChildFile ("Vocal Chop.wav");
    writeSineFile (sample, 2.0, 220.0);
    REQUIRE (processor.loadSample (0, sample).wasOk());

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    REQUIRE (editor != nullptr);

    // Let timers and the background preview render run before each picture.
    const auto pump = [] (int milliseconds)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;

        while (juce::Time::getMillisecondCounter() < end)
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, true);
    };

    pump (1200);
    const auto collapsed = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    const auto openPage = [&] (const char* name)
    {
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == name)
                button->triggerClick();

        pump (600);
        return editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    };

    const auto presets = openPage ("Presets");
    const auto mixer = openPage ("Mixer");
    const auto expanded = openPage ("Advanced");

    // The Master tab of the Advanced page (its button is inside the layer editor).
    const std::function<void (juce::Component&)> clickMaster = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == "Master")
                button->triggerClick();

            clickMaster (*child);
        }
    };

    clickMaster (*editor);
    pump (600);
    const auto master = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    // Every page fits the same window.
    REQUIRE (presets.getBounds() == collapsed.getBounds());
    REQUIRE (mixer.getBounds() == collapsed.getBounds());
    REQUIRE (expanded.getBounds() == collapsed.getBounds());

    const auto save = [directory] (const juce::Image& image, const char* name)
    {
        const auto file = juce::File (directory).getChildFile (name);
        file.deleteFile();
        INFO (name << ": " << image.getWidth() << " x " << image.getHeight());
        REQUIRE (image.isValid());
        juce::FileOutputStream stream (file);
        REQUIRE (stream.openedOk());
        REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
    };

    save (collapsed, "editor.png");
    save (presets, "editor-presets.png");
    save (mixer, "editor-mixer.png");
    save (expanded, "editor-advanced.png");
    save (master, "editor-master.png");

    // The Presets page while another preset is being auditioned.
    openPage ("Presets");
    REQUIRE (processor.auditionPreset (processor.getPresetManager().indexOf ("Sub Drop 04 808 Dive")).wasOk());
    pump (1500);
    save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), "editor-audition.png");

    // The same with a short body and a long tail.
    REQUIRE (processor.auditionPreset (processor.getPresetManager().indexOf ("Impact 09 Cinematic")).wasOk());
    setParameter (processor, ParamIDs::lengthValue, 0.5f);
    pump (1500);
    save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), "editor-audition-short.png");
}

TEST_CASE ("Loading a preset while a sound plays never continues it from the middle", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    FXtendAudioProcessor processor (storage.root);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    const auto runBlocks = [&] (int count)
    {
        float peak = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
            midi.clear();
            peak = std::max (peak, buffer.getMagnitude (0, 512));
        }

        return peak;
    };

    // Host stopped: a riser is half way through when another preset is chosen.
    REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("Riser 01 Classic Noise")).wasOk());
    runBlocks (4);
    processor.triggerPreview();
    runBlocks (200);
    const double phaseBefore = processor.getEngine().getStatus().phase.load();
    REQUIRE (phaseBefore > 0.2);

    REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("Riser 02 Tonal")).wasOk());
    runBlocks (2);

    // It plays again from the start rather than carrying on.
    const double phaseAfter = processor.getEngine().getStatus().phase.load();
    REQUIRE (phaseAfter >= 0.0);
    REQUIRE (phaseAfter < 0.05);
    REQUIRE (processor.getEngine().getStatus().activeVoices.load() == 1);

    // Nothing sounding: loading stays silent.
    runBlocks (3000);
    REQUIRE (processor.getEngine().getStatus().activeVoices.load() == 0);
    REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("Impact 01 Deep")).wasOk());
    REQUIRE (runBlocks (20) == 0.0f);
}

TEST_CASE ("Auditioning plays a preset from the start without loading it", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage storage;
    FXtendAudioProcessor processor (storage.root);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    const auto runBlocks = [&] (int count)
    {
        float peak = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
            peak = std::max (peak, buffer.getMagnitude (0, 512));
        }

        return peak;
    };

    // From silence: it plays, and nothing is loaded.
    REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("Impact 01 Deep")).wasOk());
    runBlocks (4);
    REQUIRE_FALSE (processor.isSounding());
    const auto loaded = processor.getCurrentPatch();
    const int riser = processor.getPresetManager().indexOf ("Riser 01 Classic Noise");
    const int tonal = processor.getPresetManager().indexOf ("Riser 02 Tonal");

    REQUIRE (processor.auditionPreset (riser).wasOk());
    REQUIRE (processor.getAuditionIndex() == riser);
    REQUIRE (runBlocks (100) > 0.001f);
    REQUIRE (processor.isSounding());
    REQUIRE (processor.getEngine().getStatus().phase.load() > 0.1);     // an impact would be over by now
    REQUIRE (processor.getCurrentPresetName() == "Impact 01 Deep");
    REQUIRE (processor.getCurrentPatch().fxType == loaded.fxType);
    REQUIRE (processor.getCurrentPatch().synth[0].type == loaded.synth[0].type);

    // Another one while it plays: one voice, from the start.
    REQUIRE (processor.auditionPreset (tonal).wasOk());
    runBlocks (2);
    REQUIRE (processor.getAuditionIndex() == tonal);
    REQUIRE (processor.getEngine().getStatus().phase.load() < 0.05);
    REQUIRE (processor.getEngine().getStatus().activeVoices.load() == 1);

    // Stop: silent at once, tail included.
    processor.stopSound();
    runBlocks (1);
    REQUIRE (runBlocks (20) == 0.0f);
    REQUIRE (processor.getAuditionIndex() == -1);

    // It ends by itself when the sound is over.
    REQUIRE (processor.auditionPreset (riser).wasOk());
    runBlocks (3000);
    REQUIRE (processor.getAuditionIndex() == -1);

    // A played note stops the audition and sounds the loaded preset.
    REQUIRE (processor.auditionPreset (riser).wasOk());
    runBlocks (50);
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    runBlocks (1);
    midi.clear();
    REQUIRE (processor.getAuditionIndex() == -1);
    REQUIRE (processor.getEngine().getPatch().fxType == loaded.fxType);

    // Loading by name during an audition only stops it.
    REQUIRE (processor.auditionPreset (riser).wasOk());
    runBlocks (50);
    REQUIRE (processor.loadPreset (tonal).wasOk());
    runBlocks (1);
    REQUIRE (runBlocks (20) == 0.0f);
    REQUIRE (processor.getCurrentPresetName() == "Riser 02 Tonal");
}
