#include "PluginTestSupport.h"
#include "TestHost.h"
#include "engine/FxTemplates.h"
#include "engine/WavExport.h"
#include "preset/PresetPackage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fxtend;
using namespace fxtest;

namespace
{
    struct WavContents
    {
        double sampleRate = 0.0;
        int bitsPerSample = 0;
        bool floatingPoint = false;
        int numChannels = 0;
        juce::AudioBuffer<float> audio;
    };

    WavContents readWav (const juce::File& file)
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (file.createInputStream().release(), true));
        REQUIRE (reader != nullptr);

        WavContents contents;
        contents.sampleRate = reader->sampleRate;
        contents.bitsPerSample = (int) reader->bitsPerSample;
        contents.floatingPoint = reader->usesFloatingPointData;
        contents.numChannels = (int) reader->numChannels;
        contents.audio.setSize (2, (int) reader->lengthInSamples);
        reader->read (&contents.audio, 0, (int) reader->lengthInSamples, 0, true, true);
        return contents;
    }

    RenderRequest oneBarRequest (FxType type, TailMode mode)
    {
        RenderRequest request;
        request.patch = makeTemplate (type);
        request.patch.tailMode = mode;
        request.settings.lengthValue = 1.0;
        request.bpm = 128.0;
        return request;
    }
}

TEST_CASE ("Exported WAVs have the requested format and exactly the FX length", "[export]")
{
    TempStorage storage;
    const auto target = storage.root.getChildFile ("out.wav");

    SECTION ("Strict riser, 24-bit / 48 kHz: one bar at 128 BPM is 90000 samples")
    {
        const auto request = oneBarRequest (FxType::riser, TailMode::strict);
        REQUIRE (exportWav (request, target, {}).wasOk());

        const auto wav = readWav (target);
        REQUIRE (wav.sampleRate == Catch::Approx (48000.0));
        REQUIRE (wav.bitsPerSample == 24);
        REQUIRE_FALSE (wav.floatingPoint);
        REQUIRE (wav.numChannels == 2);
        REQUIRE (wav.audio.getNumSamples() == 90000);

        // Same audio as the engine renders (within 24-bit rounding).
        auto reference = request;
        reference.sampleRate = 48000.0;
        const auto rendered = renderOffline (reference);
        float difference = 0.0f;

        for (int n = 0; n < 90000; ++n)
            difference = std::max (difference, std::abs (wav.audio.getSample (0, n) - rendered.audio.getSample (0, n)));

        REQUIRE (difference < 1.0e-5f);
        REQUIRE (wav.audio.getMagnitude (0, 90000) > 0.05f);
    }

    SECTION ("Other sample rates keep the length in time")
    {
        WavExportOptions options;
        options.sampleRate = 44100.0;
        REQUIRE (exportWav (oneBarRequest (FxType::subDrop, TailMode::strict), target, options).wasOk());
        REQUIRE (readWav (target).audio.getNumSamples() == 82688);         // 1.875 s

        options.sampleRate = 96000.0;
        options.floatingPoint = true;
        REQUIRE (exportWav (oneBarRequest (FxType::subDrop, TailMode::strict), target, options).wasOk());

        const auto wav = readWav (target);
        REQUIRE (wav.sampleRate == Catch::Approx (96000.0));
        REQUIRE (wav.bitsPerSample == 32);
        REQUIRE (wav.floatingPoint);
        REQUIRE (wav.audio.getNumSamples() == 180000);
    }

    SECTION ("A Natural tail is included after the body")
    {
        REQUIRE (exportWav (oneBarRequest (FxType::impact, TailMode::natural), target, {}).wasOk());

        const auto wav = readWav (target);
        REQUIRE (wav.audio.getNumSamples() > 90000 + 24000);
        REQUIRE (wav.audio.getNumSamples() <= 90000 + 10 * 48000);
        REQUIRE (wav.audio.getMagnitude (90000, 12000) > 1.0e-3f);

        // Ends once the tail has died away, not with a hard cut.
        REQUIRE (wav.audio.getMagnitude (wav.audio.getNumSamples() - 480, 480) < 1.0e-3f);
    }

    SECTION ("Velocity sets the level; 100 is the default")
    {
        auto request = oneBarRequest (FxType::subDrop, TailMode::strict);
        request.patch.master.limiter = 0.0f;

        REQUIRE (exportWav (request, target, {}).wasOk());
        const float normal = readWav (target).audio.getMagnitude (0, 90000);

        WavExportOptions soft;
        soft.velocity = 50;
        REQUIRE (exportWav (request, target, soft).wasOk());
        const float quiet = readWav (target).audio.getMagnitude (0, 90000);

        REQUIRE (quiet < 0.5f * normal);
        REQUIRE (quiet > 0.0f);
    }

    SECTION ("Sample layers in Stretch mode are stretched for the export tempo")
    {
        RenderRequest request;
        request.patch.samples[0].levelDb = 0.0f;
        request.patch.master.limiter = 0.0f;
        request.settings.lengthValue = 2.0;         // 3.75 s at 128 BPM
        request.bpm = 128.0;
        request.samples[0] = makeSineSample (3.0, 440.0);

        REQUIRE (exportWav (request, target, {}).wasOk());

        const auto wav = readWav (target);
        REQUIRE (wav.audio.getNumSamples() == 180000);

        const std::vector<float> left (wav.audio.getReadPointer (0), wav.audio.getReadPointer (0) + wav.audio.getNumSamples());
        REQUIRE (estimateFrequency (left, 24000, 160000, 48000.0) == Catch::Approx (440.0).epsilon (0.005));
    }

    SECTION ("A failed or cancelled export leaves no file behind")
    {
        const auto request = oneBarRequest (FxType::riser, TailMode::strict);

        REQUIRE (exportWav (request, target, {}, [] { return true; }).failed());
        REQUIRE_FALSE (target.existsAsFile());

        const auto impossible = storage.root.getChildFile ("no such folder/out.wav");
        REQUIRE (exportWav (request, impossible, {}).failed());
        REQUIRE_FALSE (impossible.existsAsFile());
        REQUIRE (storage.root.getNumberOfChildFiles (juce::File::findFiles) == 0);
    }
}

TEST_CASE ("A preset package carries the sound and its samples to another computer", "[export][package]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempStorage studio, laptop, transfer;

    const auto sampleFile = studio.root.getChildFile ("recordings/whoosh.wav");
    writeSineFile (sampleFile, 1.5, 300.0);
    const auto package = transfer.root.getChildFile ("My FX.fxtendpack");

    Patch originalPatch;
    juce::String sampleId;

    {
        FXtendAudioProcessor processor (studio.root);
        REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("Sweep 01 Up")).wasOk());
        REQUIRE (processor.loadSample (3, sampleFile).wasOk());
        setParameter (processor, ParamIDs::macro (0), 0.25f);
        REQUIRE (processor.saveUserPreset ("My FX").wasOk());

        originalPatch = processor.getCurrentPatch();
        sampleId = processor.getSampleInfo (3).ref.id;
        REQUIRE (processor.exportPresetPackage (package).wasOk());
    }

    REQUIRE (package.existsAsFile());

    SECTION ("Importing on a computer that has never seen the sample")
    {
        FXtendAudioProcessor processor (laptop.root);
        REQUIRE (processor.importPresetPackage (package).wasOk());

        REQUIRE (processor.getCurrentPresetName() == "My FX");
        REQUIRE (processor.getCurrentPatch().fxType == FxType::sweep);
        REQUIRE (processor.getCurrentPatch().macros[0] == Catch::Approx (0.25f));

        const auto info = processor.getSampleInfo (3);
        REQUIRE (info.loaded);
        REQUIRE_FALSE (info.missing);
        REQUIRE (info.ref.id == sampleId);
        REQUIRE (info.ref.name == "whoosh.wav");
        REQUIRE (info.seconds == Catch::Approx (1.5));

        // It is now one of this computer's user presets.
        REQUIRE (processor.getPresetManager().indexOf ("My FX") >= processor.getPresetManager().getNumFactoryPresets());
        REQUIRE (laptop.root.getChildFile ("Samples").getNumberOfChildFiles (juce::File::findFiles) == 1);
    }

    SECTION ("The package does not say where the sample came from on the first computer")
    {
        juce::ZipFile zip (package);
        const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (*zip.getEntry ("preset.json")));
        const auto json = stream->readEntireStreamAsString();

        REQUIRE (json.contains ("whoosh.wav"));
        REQUIRE_FALSE (json.contains ("recordings"));
        REQUIRE_FALSE (json.contains (studio.root.getFileName()));
    }

    SECTION ("Importing never replaces a preset that is already there")
    {
        FXtendAudioProcessor processor (laptop.root);
        REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("Impact 01 Deep")).wasOk());
        REQUIRE (processor.saveUserPreset ("My FX").wasOk());

        REQUIRE (processor.importPresetPackage (package).wasOk());
        REQUIRE (processor.getCurrentPresetName() == "My FX (imported)");
        REQUIRE (processor.importPresetPackage (package).wasOk());
        REQUIRE (processor.getCurrentPresetName() == "My FX (imported 2)");

        // The user's own "My FX" is still the impact it was.
        REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("My FX")).wasOk());
        REQUIRE (processor.getCurrentPatch().fxType == FxType::impact);
    }

    SECTION ("A package whose sample was altered is refused and nothing is installed")
    {
        // Rebuild the package with different audio under the same name.
        juce::ZipFile original (package);
        juce::ZipFile::Builder builder;

        for (int i = 0; i < original.getNumEntries(); ++i)
        {
            const auto* entry = original.getEntry (i);

            if (entry->filename.startsWith ("samples/"))
            {
                const auto forged = transfer.root.getChildFile ("forged.wav");
                writeSineFile (forged, 1.5, 999.0);
                builder.addFile (forged, 0, entry->filename);
            }
            else
            {
                builder.addEntry (std::unique_ptr<juce::InputStream> (original.createStreamForEntry (i)), 9, entry->filename,
                                  juce::Time::getCurrentTime());
            }
        }

        const auto tampered = transfer.root.getChildFile ("tampered.fxtendpack");

        {
            juce::FileOutputStream stream (tampered);
            REQUIRE (builder.writeToStream (stream, nullptr));
        }

        FXtendAudioProcessor processor (laptop.root);
        const auto before = processor.getCurrentPresetName();

        const auto result = processor.importPresetPackage (tampered);
        REQUIRE (result.failed());
        REQUIRE (processor.getCurrentPresetName() == before);
        REQUIRE (laptop.root.getChildFile ("Samples").getNumberOfChildFiles (juce::File::findFiles) == 0);
    }

    SECTION ("Things that are not packages are refused")
    {
        FXtendAudioProcessor processor (laptop.root);

        const auto text = transfer.root.getChildFile ("readme.fxtendpack");
        text.replaceWithText ("not a zip");
        REQUIRE (processor.importPresetPackage (text).failed());
        REQUIRE (processor.importPresetPackage (sampleFile).failed());
        REQUIRE (processor.importPresetPackage (transfer.root.getChildFile ("nothing here.fxtendpack")).failed());
    }

    SECTION ("A preset whose sample is missing cannot be packaged")
    {
        REQUIRE (sampleFile.deleteFile());
        REQUIRE (studio.root.getChildFile ("Samples").deleteRecursively());

        FXtendAudioProcessor processor (studio.root);
        REQUIRE (processor.loadPreset (processor.getPresetManager().indexOf ("My FX")).wasOk());
        REQUIRE (processor.getSampleInfo (3).missing);

        const auto incomplete = transfer.root.getChildFile ("incomplete.fxtendpack");
        const auto result = processor.exportPresetPackage (incomplete);
        REQUIRE (result.failed());
        REQUIRE (result.getErrorMessage().contains ("whoosh.wav"));
        REQUIRE_FALSE (incomplete.existsAsFile());
    }

    SECTION ("A package without samples (pure synthesis) works too")
    {
        FXtendAudioProcessor source (studio.root);
        REQUIRE (source.loadPreset (source.getPresetManager().indexOf ("Impact 03 Metallic")).wasOk());
        const auto plain = transfer.root.getChildFile ("plain.fxtendpack");
        REQUIRE (source.exportPresetPackage (plain).wasOk());

        FXtendAudioProcessor processor (laptop.root);
        REQUIRE (processor.importPresetPackage (plain).wasOk());
        REQUIRE (processor.getCurrentPatch().fxType == FxType::impact);

        // Factory names are taken, so the import is kept under its own name.
        REQUIRE (processor.getCurrentPresetName() == "Impact 03 Metallic (imported)");
        REQUIRE (processor.getPresetManager().indexOf ("Impact 03 Metallic (imported)") >= processor.getPresetManager().getNumFactoryPresets());
    }
}
