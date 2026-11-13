#include "TestHost.h"
#include "engine/FxTemplates.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <cstdlib>

using namespace fxtend;
using fxtest::HostSim;

namespace
{
    constexpr long long oneBar = 90000;     // 1 bar at 128 BPM, 48 kHz

    std::vector<std::vector<float>> renderPatch (const Patch& patch, double bars = 1.0, int blockSize = 512,
                                                 long long totalSamples = 2 * 48000)
    {
        HostSim host;
        host.blockSize = blockSize;
        host.events = { { 0, true } };

        FxEngine engine;
        EngineSettings settings;
        settings.lengthValue = bars;
        engine.setSettings (settings);
        engine.setPatch (patch);
        engine.prepare (host.sampleRate, host.blockSize);
        return host.render (engine, totalSamples);
    }

    double rms (const std::vector<std::vector<float>>& audio, long long from, long long to)
    {
        double sum = 0.0;

        for (long long n = from; n < to; ++n)
            sum += 0.5 * ((double) audio[0][(size_t) n] * audio[0][(size_t) n]
                          + (double) audio[1][(size_t) n] * audio[1][(size_t) n]);

        return std::sqrt (sum / (double) std::max<long long> (1, to - from));
    }

    bool allFinite (const std::vector<std::vector<float>>& audio)
    {
        for (const auto& channel : audio)
            for (float sample : channel)
                if (! std::isfinite (sample))
                    return false;

        return true;
    }
}

TEST_CASE ("Curves start at 0, end at 1 and bend the right way", "[synth]")
{
    for (auto shape : { CurveShape::linear, CurveShape::exponential, CurveShape::sCurve, CurveShape::custom })
    {
        for (float tension : { -1.0f, -0.3f, 0.0f, 0.6f, 1.0f })
        {
            REQUIRE (evaluateCurve (shape, tension, defaultCurvePoints, 0.0f) == Catch::Approx (0.0f).margin (1.0e-5));
            REQUIRE (evaluateCurve (shape, tension, defaultCurvePoints, 1.0f) == Catch::Approx (1.0f).margin (1.0e-5));

            float previous = 0.0f;

            for (int i = 1; i <= 100; ++i)
            {
                const float value = evaluateCurve (shape, tension, defaultCurvePoints, (float) i / 100.0f);
                REQUIRE (value >= previous - 1.0e-5f);
                previous = value;
            }
        }
    }

    REQUIRE (evaluateCurve (CurveShape::linear, 0.0f, defaultCurvePoints, 0.5f) == Catch::Approx (0.5f));
    REQUIRE (evaluateCurve (CurveShape::exponential, 0.8f, defaultCurvePoints, 0.5f) < 0.3f);
    REQUIRE (evaluateCurve (CurveShape::exponential, -0.8f, defaultCurvePoints, 0.5f) > 0.7f);
    REQUIRE (evaluateCurve (CurveShape::sCurve, 0.0f, defaultCurvePoints, 0.25f) < 0.25f);
    REQUIRE (evaluateCurve (CurveShape::sCurve, 0.0f, defaultCurvePoints, 0.75f) > 0.75f);

    // A custom curve passes through its points and may come back down.
    const CurvePoints bell { 0.0f, 1.0f, 1.0f, 0.0f };
    REQUIRE (evaluateCurve (CurveShape::custom, 0.0f, bell, 1.0f / 3.0f) == Catch::Approx (1.0f).margin (1.0e-4));
    REQUIRE (evaluateCurve (CurveShape::custom, 0.0f, bell, 1.0f) == Catch::Approx (0.0f).margin (1.0e-4));
}

TEST_CASE ("Every FX type sounds with synthesis alone and ends on time", "[synth]")
{
    const int typeIndex = GENERATE (range (0, numFxTypes));
    const auto type = (FxType) typeIndex;
    INFO (getFxTypeName (type));

    Patch patch = makeTemplate (type);
    patch.tailMode = TailMode::strict;
    const auto audio = renderPatch (patch);

    REQUIRE (allFinite (audio));
    REQUIRE (fxtest::peakBetween (audio, 0, oneBar) > 0.05f);
    REQUIRE (fxtest::peakBetween (audio, 0, oneBar) <= 1.0f);
    REQUIRE (fxtest::lastAudible (audio) < oneBar);
}

TEST_CASE ("FX types move in the direction their name promises", "[synth]")
{
    const long long tenth = oneBar / 10;

    SECTION ("Riser gets louder towards the end")
    {
        const auto audio = renderPatch (makeTemplate (FxType::riser));
        REQUIRE (rms (audio, 8 * tenth, oneBar - 480) > 4.0 * rms (audio, 0, 2 * tenth));
    }

    SECTION ("Downlifter gets quieter towards the end")
    {
        const auto audio = renderPatch (makeTemplate (FxType::downlifter));
        REQUIRE (rms (audio, 0, 2 * tenth) > 4.0 * rms (audio, 8 * tenth, oneBar));
    }

    SECTION ("Impact hits at the start and has decayed by the end")
    {
        const auto audio = renderPatch (makeTemplate (FxType::impact));
        REQUIRE (fxtest::firstAudible (audio) <= 1);
        REQUIRE (fxtest::peakBetween (audio, 0, 2400) > 0.3f);

        // The template's compressor lifts the body after the hit, so the fall
        // is smaller than that of the bare layers - but it is still a clear decay.
        REQUIRE (rms (audio, 0, tenth) > 3.0 * rms (audio, 8 * tenth, oneBar));

        Patch bare = makeTemplate (FxType::impact);
        bare.master.compAmount = 0.0f;
        bare.master.limiterBoostDb = 0.0f;
        const auto bareAudio = renderPatch (bare);
        REQUIRE (rms (bareAudio, 0, tenth) > 8.0 * rms (bareAudio, 8 * tenth, oneBar));
    }

    SECTION ("Sweep builds all the way to the end point")
    {
        const auto audio = renderPatch (makeTemplate (FxType::sweep));
        REQUIRE (rms (audio, 9 * tenth, oneBar - 480) > 1.5 * rms (audio, 5 * tenth, 7 * tenth));
        REQUIRE (rms (audio, 9 * tenth, oneBar - 480) > 6.0 * rms (audio, 0, 2 * tenth));
    }

    SECTION ("Clap is loudest in its first moments")
    {
        const auto audio = renderPatch (makeTemplate (FxType::clap));
        REQUIRE (rms (audio, 0, 4800) > 5.0 * rms (audio, 5 * tenth, oneBar));
    }

    SECTION ("Impact and Clap hit just as hard when the body is long")
    {
        // Their attack runs in real time; only the ring-out follows the length.
        for (const auto type : { FxType::impact, FxType::clap })
        {
            Patch patch = makeTemplate (type);
            patch.tailMode = TailMode::strict;
            patch.master.reverbMix = 0.0f;

            const auto oneBarLong = renderPatch (patch, 1.0, 512, 3 * 48000);
            const auto eightBarsLong = renderPatch (patch, 8.0, 512, 3 * 48000);

            INFO (getFxTypeName (type));
            REQUIRE (rms (eightBarsLong, 0, 2400) > 0.8 * rms (oneBarLong, 0, 2400));
            REQUIRE (rms (eightBarsLong, 0, 2400) < 1.25 * rms (oneBarLong, 0, 2400));

            if (type == FxType::clap)
                REQUIRE (rms (eightBarsLong, 0, 4800) > 20.0 * rms (eightBarsLong, 48000, 2 * 48000));
        }
    }

    SECTION ("Sub Drop holds its level to the end")
    {
        const auto audio = renderPatch (makeTemplate (FxType::subDrop));
        REQUIRE (rms (audio, 8 * tenth, oneBar - 480) > 0.3 * rms (audio, tenth, 3 * tenth));
    }
}

TEST_CASE ("Rendering is repeatable and independent of the buffer size", "[synth]")
{
    const int typeIndex = GENERATE (range (0, numFxTypes));
    const auto patch = makeTemplate ((FxType) typeIndex);
    INFO (getFxTypeName ((FxType) typeIndex));

    const auto reference = renderPatch (patch, 1.0, 512);
    const auto again = renderPatch (patch, 1.0, 512);
    REQUIRE (reference == again);

    for (int blockSize : { 32, 100, 1024 })
    {
        const auto other = renderPatch (patch, 1.0, blockSize);
        float maxDifference = 0.0f;

        for (size_t n = 0; n < reference[0].size(); ++n)
            maxDifference = std::max ({ maxDifference,
                                        std::abs (reference[0][n] - other[0][n]),
                                        std::abs (reference[1][n] - other[1][n]) });

        INFO ("block size " << blockSize);
        REQUIRE (maxDifference < 2.0e-3f);
    }
}

TEST_CASE ("Neutral macros leave the patch alone; moved macros change the sound", "[synth]")
{
    const int typeIndex = GENERATE (range (0, numFxTypes));
    const auto type = (FxType) typeIndex;
    INFO (getFxTypeName (type));

    Patch base = makeTemplate (type);
    base.tailMode = TailMode::strict;
    const Patch neutral = applyMacros (base);
    REQUIRE (neutral.master.reverbMix == Catch::Approx (base.master.reverbMix));

    for (size_t i = 0; i < base.synth.size(); ++i)
    {
        REQUIRE (neutral.synth[i].levelDb == Catch::Approx (base.synth[i].levelDb));
        REQUIRE (neutral.synth[i].cutoffStart == Catch::Approx (base.synth[i].cutoffStart));
        REQUIRE (neutral.synth[i].cutoffEnd == Catch::Approx (base.synth[i].cutoffEnd));
        REQUIRE (neutral.synth[i].pitchStart == Catch::Approx (base.synth[i].pitchStart));
        REQUIRE (neutral.synth[i].pitchEnd == Catch::Approx (base.synth[i].pitchEnd));
        REQUIRE (neutral.synth[i].width == Catch::Approx (base.synth[i].width));
        REQUIRE (neutral.synth[i].count == Catch::Approx (base.synth[i].count));
        REQUIRE (neutral.synth[i].variant == Catch::Approx (base.synth[i].variant));
    }

    const auto reference = renderPatch (base);

    for (int macro = 0; macro < numMacros; ++macro)
    {
        for (float value : { 0.0f, 1.0f })
        {
            Patch moved = base;
            moved.macros[(size_t) macro] = value;
            const auto audio = renderPatch (moved);

            INFO (getMacroName (type, macro) << " = " << value);
            REQUIRE (allFinite (audio));
            REQUIRE (fxtest::peakBetween (audio, 0, oneBar) <= 1.0f);
            REQUIRE (fxtest::lastAudible (audio) < oneBar);

            if (value == 0.0f)
                REQUIRE (audio != reference);
        }
    }
}

TEST_CASE ("A layer can be limited to part of the body", "[synth]")
{
    Patch patch = fxtest::steadyNoisePatch();
    patch.synth[0].start = 0.25f;
    patch.synth[0].end = 0.5f;

    const auto audio = renderPatch (patch);

    REQUIRE (fxtest::firstAudible (audio) >= oneBar / 4 - 16);
    REQUIRE (fxtest::firstAudible (audio) <= oneBar / 4 + 16);
    REQUIRE (fxtest::lastAudible (audio) >= oneBar / 2 - 16);
    REQUIRE (fxtest::lastAudible (audio) <= oneBar / 2 + 16 + 200);    // plus the layer's short fade
}

// Writes each FX type to a WAV for listening:
//   FXTEND_RENDER_DIR=/some/folder FXtendTests "[.render]"
TEST_CASE ("Render FX types to WAV files", "[.render]")
{
    const char* directory = std::getenv ("FXTEND_RENDER_DIR");
    REQUIRE (directory != nullptr);

    for (int typeIndex = 0; typeIndex < numFxTypes; ++typeIndex)
    {
        const auto type = (FxType) typeIndex;
        const char* barsText = std::getenv ("FXTEND_RENDER_BARS");
        const double bars = barsText != nullptr ? std::atof (barsText)
                                                : ((type == FxType::clap || type == FxType::impact) ? 1.0 : 2.0);
        const auto audio = renderPatch (makeTemplate (type), bars, 512, (long long) ((bars * 1.875 + 3.0) * 48000.0));

        juce::AudioBuffer<float> buffer (2, (int) audio[0].size());
        buffer.copyFrom (0, 0, audio[0].data(), buffer.getNumSamples());
        buffer.copyFrom (1, 0, audio[1].data(), buffer.getNumSamples());

        const auto file = juce::File (directory).getChildFile (juce::String (getFxTypeName (type)).replaceCharacter (' ', '_') + ".wav");
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        REQUIRE (stream != nullptr);

        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000.0)
                                                                                    .withNumChannels (2)
                                                                                    .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()));
    }
}
