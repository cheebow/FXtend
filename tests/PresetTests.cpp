#include "TestHost.h"
#include "engine/FxTemplates.h"
#include "engine/LayerFields.h"
#include "engine/OfflineRenderer.h"
#include "preset/FactoryPresets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include "preset/PresetManager.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

using namespace fxtend;

namespace
{
    void requireSamePatch (const Patch& a, const Patch& b)
    {
        REQUIRE (a.fxType == b.fxType);
        REQUIRE (a.tailMode == b.tailMode);
        REQUIRE (a.masterGainDb == Catch::Approx (b.masterGainDb));

        for (size_t i = 0; i < a.macros.size(); ++i)
            REQUIRE (a.macros[i] == Catch::Approx (b.macros[i]));

        for (const auto& field : getMasterFields())
        {
            INFO ("master field " << field.id);
            REQUIRE (a.master.*(field.member) == Catch::Approx (b.master.*(field.member)).margin (1.0e-4));
        }

        for (size_t layer = 0; layer < a.synth.size(); ++layer)
        {
            for (const auto& field : getSynthLayerFields())
            {
                INFO ("layer " << layer << " field " << field.id);
                REQUIRE (a.synth[layer].*(field.member) == Catch::Approx (b.synth[layer].*(field.member)).margin (1.0e-4));
            }

            for (size_t i = 0; i < 4; ++i)
            {
                REQUIRE (a.synth[layer].ampPoints[i] == Catch::Approx (b.synth[layer].ampPoints[i]).margin (1.0e-5));
                REQUIRE (a.synth[layer].pitchPoints[i] == Catch::Approx (b.synth[layer].pitchPoints[i]).margin (1.0e-5));
                REQUIRE (a.synth[layer].cutoffPoints[i] == Catch::Approx (b.synth[layer].cutoffPoints[i]).margin (1.0e-5));
            }
        }
    }

    struct TempFolder
    {
        TempFolder() : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("fxtend-tests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
        {
            folder.createDirectory();
        }

        ~TempFolder() { folder.deleteRecursively(); }

        juce::File folder;
    };
}

TEST_CASE ("Factory presets cover every FX type as specified", "[preset]")
{
    const auto& presets = getFactoryPresets();
    REQUIRE (presets.size() >= 30);

    std::map<FxType, int> counts;
    std::set<juce::String> names;

    for (const auto& preset : presets)
    {
        ++counts[preset.patch.fxType];
        names.insert (preset.name);
    }

    REQUIRE (names.size() == presets.size());
    REQUIRE (counts[FxType::impact] >= 8);
    REQUIRE (counts[FxType::riser] >= 6);
    REQUIRE (counts[FxType::downlifter] >= 6);
    REQUIRE (counts[FxType::sweep] >= 4);
    REQUIRE (counts[FxType::clap] >= 4);
    REQUIRE (counts[FxType::subDrop] >= 2);
}

TEST_CASE ("Every factory preset sounds with synthesis alone, stays in range and ends on time", "[preset]")
{
    for (const auto& preset : getFactoryPresets())
    {
        INFO (preset.name);

        if (preset.name == emptyPresetName)
            continue;       // has its own test below

        fxtest::HostSim host;
        host.events = { { 0, true, 60, 127 } };

        FxEngine engine;
        EngineSettings settings;
        settings.lengthValue = 1.0;
        engine.setSettings (settings);
        Patch strict = preset.patch;
        strict.tailMode = TailMode::strict;
        engine.setPatch (strict);
        engine.prepare (host.sampleRate, host.blockSize);

        const auto audio = host.render (engine, 2 * 48000);
        const float peak = fxtest::peakBetween (audio, 0, 90000);

        // Loud enough to use, and the limiter keeps even a full-velocity hit below full scale.
        REQUIRE (peak > 0.05f);
        REQUIRE (peak <= 1.0f);
        REQUIRE (fxtest::lastAudible (audio) < 90000);

        for (const auto& field : getMasterFields())
        {
            INFO (field.id);
            REQUIRE (preset.patch.master.*(field.member) >= field.min);
            REQUIRE (preset.patch.master.*(field.member) <= field.max);
        }

        // Every value stays inside the range the host parameters allow.
        for (const auto& layer : preset.patch.synth)
        {
            for (const auto& field : getSynthLayerFields())
            {
                INFO (field.id);
                REQUIRE (layer.*(field.member) >= field.min);
                REQUIRE (layer.*(field.member) <= field.max);
            }
        }
    }
}

TEST_CASE ("Presets survive a round trip through JSON", "[preset]")
{
    for (const auto& preset : getFactoryPresets())
    {
        INFO (preset.name);

        PresetData loaded;
        REQUIRE (PresetSerializer::fromJson (PresetSerializer::toJson (preset), loaded).wasOk());
        REQUIRE (loaded.name == preset.name);
        requireSamePatch (loaded.patch, preset.patch);
    }
}

TEST_CASE ("Preset loading is strict about what it accepts and lenient about what is missing", "[preset]")
{
    PresetData preset;
    preset.name = "untouched";

    REQUIRE (PresetSerializer::fromJson ("not json at all {", preset).failed());
    REQUIRE (PresetSerializer::fromJson ("{\"format\":\"something-else\",\"version\":1}", preset).failed());
    REQUIRE (PresetSerializer::fromJson ("{\"format\":\"fxtend-preset\",\"version\":999}", preset).failed());
    REQUIRE (preset.name == "untouched");

    // Only a few values given: the rest fall back to defaults, and out-of-range values are clamped.
    const auto result = PresetSerializer::fromJson (R"({"format":"fxtend-preset","version":1,"name":"Sparse","fxType":"impact",
                                                       "synth":[{"on":true,"type":1,"level":999,"baseHz":60}]})", preset);
    REQUIRE (result.wasOk());
    REQUIRE (preset.name == "Sparse");
    REQUIRE (preset.patch.fxType == FxType::impact);
    REQUIRE (preset.patch.synth[0].isEnabled());
    REQUIRE (preset.patch.synth[0].getType() == SynthType::sub);
    REQUIRE (preset.patch.synth[0].levelDb == Catch::Approx (12.0f));
    REQUIRE (preset.patch.synth[0].baseHz == Catch::Approx (60.0f));
    REQUIRE (preset.patch.synth[0].end == Catch::Approx (1.0f));
    REQUIRE_FALSE (preset.patch.synth[1].isEnabled());
    REQUIRE (preset.patch.macros[0] == Catch::Approx (0.5f));
}

TEST_CASE ("User presets can be saved, listed, loaded and renamed", "[preset]")
{
    TempFolder temp;
    PresetManager manager (temp.folder.getChildFile ("Presets"));

    const int numFactory = manager.getNumFactoryPresets();
    REQUIRE (manager.getNumPresets() == numFactory);
    REQUIRE (numFactory == (int) getFactoryPresets().size());

    PresetData mine;
    mine.patch = makeTemplate (FxType::sweep);
    mine.patch.macros = { 0.1f, 0.2f, 0.3f, 0.4f };
    mine.patch.synth[0].ampPoints = { 0.0f, 0.25f, 1.0f, 0.5f };

    REQUIRE (manager.saveUser ("My Sweep", mine).wasOk());
    REQUIRE (manager.getNumPresets() == numFactory + 1);

    const int index = manager.indexOf ("My Sweep");
    REQUIRE (index >= numFactory);
    REQUIRE_FALSE (manager.isFactory (index));
    REQUIRE (manager.getFxType (index) == FxType::sweep);

    PresetData loaded;
    REQUIRE (manager.load (index, loaded).wasOk());
    REQUIRE (loaded.name == "My Sweep");
    requireSamePatch (loaded.patch, mine.patch);

    // Names that would clash with factory presets or are not file names are refused.
    REQUIRE (manager.saveUser (manager.getName (0), mine).failed());
    REQUIRE (manager.saveUser ("   ", mine).failed());
    REQUIRE (manager.saveUser ("bad/name", mine).failed());
    REQUIRE (manager.renameUser (0, "Renamed Factory").failed());

    REQUIRE (manager.renameUser (index, "My Better Sweep").wasOk());
    REQUIRE (manager.indexOf ("My Sweep") == -1);
    REQUIRE (manager.indexOf ("My Better Sweep") >= numFactory);

    // A second manager looking at the same folder sees the preset: it is really on disk.
    PresetManager other (temp.folder.getChildFile ("Presets"));
    REQUIRE (other.indexOf ("My Better Sweep") >= numFactory);
}

// Prints level figures for every factory preset (velocity 100, 2 bars at 128 BPM):
//   FXtendTests "[.levels]"
TEST_CASE ("Report the levels of every factory preset", "[.levels]")
{
    std::printf ("%-28s %7s %7s %7s %8s %6s %7s %6s\n", "preset", "peak dB", "rms dB", "DC", "max step", "at %", "first", "tail s");

    for (const auto& preset : getFactoryPresets())
    {
        RenderRequest request;
        request.patch = preset.patch;
        request.settings.lengthValue = 2.0;
        request.bpm = 128.0;

        const auto result = renderOffline (request);
        REQUIRE (result.completed);

        const int total = result.audio.getNumSamples();
        const int body = result.bodySamples;
        double sumSquares = 0.0, sum = 0.0;
        float peak = 0.0f, maxStep = 0.0f;
        bool finite = true;
        int maxStepAt = 0;

        for (int channel = 0; channel < 2; ++channel)
        {
            const float* data = result.audio.getReadPointer (channel);
            float previous = 0.0f;

            for (int i = 0; i < total; ++i)
            {
                finite = finite && std::isfinite (data[i]);
                peak = std::max (peak, std::abs (data[i]));
                if (std::abs (data[i] - previous) > maxStep)
                {
                    maxStep = std::abs (data[i] - previous);
                    maxStepAt = i;
                }

                previous = data[i];

                if (i < body)
                {
                    sumSquares += (double) data[i] * data[i];
                    sum += data[i];
                }
            }
        }

        INFO (preset.name);
        REQUIRE (finite);

        const float first = std::max (std::abs (result.audio.getSample (0, 0)), std::abs (result.audio.getSample (1, 0)));

        std::printf ("%-28s %7.1f %7.1f %7.4f %8.3f %6.1f %7.3f %6.2f\n",
                     preset.name.toRawUTF8(),
                     20.0 * std::log10 (std::max (peak, 1.0e-9f)),
                     10.0 * std::log10 (std::max (sumSquares / (2.0 * body), 1.0e-18)),
                     sum / (2.0 * body),
                     (double) maxStep,
                     100.0 * maxStepAt / body,
                     (double) first,
                     (double) (total - body) / request.sampleRate);
    }
}

// Writes every factory preset (with its tail) to a WAV for listening:
//   FXTEND_RENDER_DIR=/some/folder [FXTEND_RENDER_BARS=4] FXtendTests "[.presets]"
TEST_CASE ("Render every factory preset to a WAV file", "[.presets]")
{
    const char* directory = std::getenv ("FXTEND_RENDER_DIR");
    REQUIRE (directory != nullptr);
    const char* barsText = std::getenv ("FXTEND_RENDER_BARS");

    for (const auto& preset : getFactoryPresets())
    {
        RenderRequest request;
        request.patch = preset.patch;
        request.settings.lengthValue = barsText != nullptr ? std::atof (barsText) : 2.0;
        request.bpm = 128.0;

        const auto result = renderOffline (request);
        REQUIRE (result.completed);

        const auto file = juce::File (directory).getChildFile (preset.name.replaceCharacter (' ', '_') + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        REQUIRE (stream != nullptr);

        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (request.sampleRate)
                                                                                    .withNumChannels (2)
                                                                                    .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (result.audio, 0, result.audio.getNumSamples()));
    }
}

TEST_CASE ("The empty init preset has nothing switched on and is silent until something is added", "[preset]")
{
    const auto& presets = getFactoryPresets();
    const auto found = std::find_if (presets.begin(), presets.end(), [] (const PresetData& preset) { return preset.name == emptyPresetName; });
    REQUIRE (found != presets.end());

    for (const auto& layer : found->patch.synth)
        REQUIRE_FALSE (layer.isEnabled());

    REQUIRE (found->patch.master.reverbMix == 0.0f);
    REQUIRE (found->patch.master.delayMix == 0.0f);
    REQUIRE (found->patch.master.compAmount == 0.0f);

    fxtest::HostSim host;
    host.events = { { 0, true, 60, 127 } };

    FxEngine engine;
    engine.setPatch (found->patch);
    engine.prepare (host.sampleRate, host.blockSize);
    const auto audio = host.render (engine, 2 * 48000);
    REQUIRE (fxtest::peakBetween (audio, 0, 2 * 48000) == 0.0f);
}
