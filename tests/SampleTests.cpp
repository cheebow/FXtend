#include "TestHost.h"
#include "dsp/TimeStretchProcessor.h"
#include "engine/FxTemplates.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fxtend;
using fxtest::HostSim;

namespace
{
    constexpr long long oneBar = 90000;     // 1 bar at 128 BPM, 48 kHz = 1.875 s

    /** A patch that plays only sample slot 1, with no fades getting in the way of measurements. */
    Patch samplePatch (SampleMode mode)
    {
        Patch patch;
        patch.samples[0].enabled = 1.0f;
        patch.samples[0].mode = (float) (int) mode;
        patch.samples[0].levelDb = 0.0f;
        patch.samples[0].fadeInMs = 0.0f;
        patch.samples[0].fadeOutMs = 0.0f;
        patch.master.limiter = 0.0f;
        return patch;
    }

    struct Rendered
    {
        std::vector<std::vector<float>> audio;
        long long last = -1;
    };

    Rendered render (const Patch& patch, const SampleData::Ptr& sample, double bars = 1.0, long long totalSamples = 3 * 48000,
                     const SampleData::Ptr& stretched = {}, HostSim host = {})
    {
        host.events = { { 0, true, 60, 127 } };
        host.sources[0] = { sample.get(), stretched.get() };

        FxEngine engine;
        EngineSettings settings;
        settings.lengthValue = bars;
        engine.setSettings (settings);
        engine.setPatch (patch);
        engine.prepare (host.sampleRate, host.blockSize);

        Rendered result;
        result.audio = host.render (engine, totalSamples);
        result.last = fxtest::lastAudible (result.audio, 1.0e-5f);
        return result;
    }
}

TEST_CASE ("Resample: the sample is sped up or slowed down to fill the body exactly", "[sample]")
{
    // 3 s of 440 Hz squeezed into 1.875 s must sound at 440 * 3 / 1.875 = 704 Hz.
    const auto sample = fxtest::makeSineSample (3.0, 440.0);
    const auto result = render (samplePatch (SampleMode::resample), sample);

    REQUIRE (result.last < oneBar);
    REQUIRE (result.last > oneBar - 480);
    REQUIRE (fxtest::estimateFrequency (result.audio[0], 10000, 80000, 48000.0) == Catch::Approx (704.0).epsilon (0.002));
    REQUIRE (fxtest::peakBetween (result.audio, 20000, 70000) == Catch::Approx (0.5f).epsilon (0.02));
}

TEST_CASE ("One-shot: original speed and pitch whatever the file's sample rate; never stretched", "[sample]")
{
    for (const double fileRate : { 44100.0, 48000.0, 96000.0 })
    {
        INFO ("file sample rate " << fileRate);
        const auto sample = fxtest::makeSineSample (0.5, 440.0, fileRate);
        const auto result = render (samplePatch (SampleMode::oneShot), sample);

        // Half a second long, although the body lasts 1.875 s.
        REQUIRE (result.last > 24000 - 200);
        REQUIRE (result.last < 24000 + 50);
        REQUIRE (fxtest::estimateFrequency (result.audio[0], 2000, 22000, 48000.0) == Catch::Approx (440.0).epsilon (0.002));
    }

    SECTION ("A longer sample is cut at the Body End")
    {
        const auto sample = fxtest::makeSineSample (4.0, 440.0);
        const auto result = render (samplePatch (SampleMode::oneShot), sample);
        REQUIRE (result.last < oneBar);
        REQUIRE (result.last > oneBar - 480);
    }

    SECTION ("Pitch transposes a one-shot")
    {
        Patch patch = samplePatch (SampleMode::oneShot);
        patch.samples[0].pitch = 12.0f;
        const auto sample = fxtest::makeSineSample (1.0, 440.0);
        const auto result = render (patch, sample);
        REQUIRE (fxtest::estimateFrequency (result.audio[0], 2000, 20000, 48000.0) == Catch::Approx (880.0).epsilon (0.002));
        REQUIRE (result.last < 24000 + 50);     // twice as fast, so half as long
    }
}

TEST_CASE ("Loop: a short sample repeats smoothly until the Body End", "[sample]")
{
    const auto sample = fxtest::makeSineSample (0.2, 441.0);       // whole number of cycles
    const auto result = render (samplePatch (SampleMode::loop), sample);

    REQUIRE (result.last < oneBar);
    REQUIRE (result.last > oneBar - 480);

    // Sounding the whole way through, at the original pitch...
    for (long long from = 0; from < oneBar - 4800; from += 4800)
        REQUIRE (fxtest::peakBetween (result.audio, from, from + 4800) > 0.3f);

    REQUIRE (fxtest::estimateFrequency (result.audio[0], 1000, 80000, 48000.0) == Catch::Approx (441.0).epsilon (0.01));

    // ...and without clicks at the loop joins: a 441 Hz sine at 0.5 never moves more than ~0.03 per sample.
    float largestStep = 0.0f;

    for (long long n = 1; n < oneBar - 480; ++n)
        largestStep = std::max (largestStep, std::abs (result.audio[0][(size_t) n] - result.audio[0][(size_t) n - 1]));

    REQUIRE (largestStep < 0.06f);
}

TEST_CASE ("Trim and Reverse select which part of the file plays, and in which direction", "[sample]")
{
    // Level rises from silent to full over the file.
    const auto sample = fxtest::makeSineSample (2.0, 440.0, 44100.0, 0.0f, 1.0f);

    SECTION ("Forwards it gets louder, reversed it gets quieter")
    {
        const auto forwards = render (samplePatch (SampleMode::resample), sample);
        REQUIRE (fxtest::peakBetween (forwards.audio, 80000, 89000) > 3.0f * fxtest::peakBetween (forwards.audio, 2000, 12000));

        Patch patch = samplePatch (SampleMode::resample);
        patch.samples[0].reverse = 1.0f;
        const auto backwards = render (patch, sample);
        REQUIRE (fxtest::peakBetween (backwards.audio, 2000, 12000) > 3.0f * fxtest::peakBetween (backwards.audio, 80000, 89000));
    }

    SECTION ("Trimming to the second half plays only the loud half")
    {
        Patch patch = samplePatch (SampleMode::resample);
        patch.samples[0].trimStart = 0.5f;
        const auto result = render (patch, sample);

        REQUIRE (fxtest::peakBetween (result.audio, 0, 4800) > 0.45f);
        REQUIRE (fxtest::peakBetween (result.audio, 85000, 89900) > 0.9f);
        REQUIRE (result.last < oneBar);

        // Half the material over the same time: half the speed, half the pitch change.
        REQUIRE (fxtest::estimateFrequency (result.audio[0], 10000, 80000, 48000.0) == Catch::Approx (440.0 * 1.0 / 1.875).epsilon (0.003));
    }
}

TEST_CASE ("Stretch: reads the prepared audio in step with the FX; falls back to the raw file meanwhile", "[sample]")
{
    const auto raw = fxtest::makeSineSample (3.0, 440.0);

    // Stand-in for a time-stretched render: the same pitch, already 1.875 s long.
    const auto stretched = fxtest::makeSineSample (1.875, 440.0);

    const auto prepared = render (samplePatch (SampleMode::stretch), raw, 1.0, 3 * 48000, stretched);
    REQUIRE (prepared.last < oneBar);
    REQUIRE (prepared.last > oneBar - 480);
    REQUIRE (fxtest::estimateFrequency (prepared.audio[0], 10000, 80000, 48000.0) == Catch::Approx (440.0).epsilon (0.002));

    // Not ready yet: still the right length, with the pitch following the speed for now.
    const auto fallback = render (samplePatch (SampleMode::stretch), raw);
    REQUIRE (fallback.last < oneBar);
    REQUIRE (fallback.last > oneBar - 480);
    REQUIRE (fxtest::estimateFrequency (fallback.audio[0], 10000, 80000, 48000.0) == Catch::Approx (704.0).epsilon (0.002));
}

TEST_CASE ("Phase-locked sample layers end on the bar line even when the tempo changes", "[sample]")
{
    HostSim host;
    host.blockSize = 256;
    host.bpmAt = [] (long long position) { return position < 256 * 150 ? 128.0 : 90.0; };

    const auto sample = fxtest::makeSineSample (3.0, 440.0);
    const auto stretched = fxtest::makeSineSample (1.875, 440.0);
    const auto result = render (samplePatch (SampleMode::stretch), sample, 1.0, 4 * 48000, stretched, host);

    const double qnBefore = 256.0 * 150 * 128.0 / (60.0 * 48000.0);
    const auto expectedEnd = (long long) std::ceil (256.0 * 150 + (4.0 - qnBefore) * 60.0 * 48000.0 / 90.0);

    REQUIRE (result.last < expectedEnd);
    REQUIRE (result.last > expectedEnd - 480);

    // The whole file was played: its last cycles are heard just before the end, not cut off or run out early.
    REQUIRE (fxtest::peakBetween (result.audio, expectedEnd - 4800, expectedEnd - 480) > 0.4f);
}

TEST_CASE ("Layer fades and the layer's part of the body apply to samples", "[sample]")
{
    const auto sample = fxtest::makeSineSample (3.0, 440.0);

    Patch patch = samplePatch (SampleMode::resample);
    patch.samples[0].start = 0.5f;
    patch.samples[0].fadeInMs = 100.0f;
    patch.samples[0].fadeOutMs = 100.0f;

    const auto result = render (patch, sample);
    const long long half = oneBar / 2;

    REQUIRE (fxtest::peakBetween (result.audio, 0, half - 32) == 0.0f);
    REQUIRE (fxtest::peakBetween (result.audio, half, half + 480) < 0.1f);                 // fading in
    REQUIRE (fxtest::peakBetween (result.audio, half + 9600, half + 19200) > 0.45f);       // full level
    REQUIRE (fxtest::peakBetween (result.audio, oneBar - 480, oneBar) < 0.1f);             // fading out
}

TEST_CASE ("A missing sample is silent but everything else keeps playing", "[sample]")
{
    Patch patch = fxtest::steadyNoisePatch();
    patch.samples[0].enabled = 1.0f;

    const auto result = render (patch, {});
    REQUIRE (fxtest::peakBetween (result.audio, 1000, 80000) > 0.1f);
    REQUIRE (result.last < oneBar);

    const auto silent = render (samplePatch (SampleMode::stretch), {});
    REQUIRE (silent.last == -1);
}

TEST_CASE ("Replacing or removing a sample while it plays is safe", "[sample]")
{
    const auto first = fxtest::makeSineSample (3.0, 440.0);
    const auto second = fxtest::makeSineSample (1.0, 880.0);

    HostSim host;
    host.beforeBlock = [&] (long long position, HostSim& h, double&)
    {
        if (position == 512 * 40)
            h.sources[0] = { second.get(), nullptr };       // swapped for another file

        if (position == 512 * 100)
            h.sources[0] = {};                              // taken away
    };

    const auto result = render (samplePatch (SampleMode::resample), first, 1.0, 3 * 48000, {}, host);

    REQUIRE (fxtest::peakBetween (result.audio, 5000, 15000) > 0.4f);
    REQUIRE (fxtest::peakBetween (result.audio, 512 * 45, 512 * 95) > 0.4f);

    // 1 s of 880 Hz over 1.875 s.
    REQUIRE (fxtest::estimateFrequency (result.audio[0], 512 * 45, 512 * 95, 48000.0) == Catch::Approx (880.0 / 1.875).epsilon (0.01));
    REQUIRE (result.last < 512 * 101);
}

TEST_CASE ("A block without access to the sample audio is a gap, not the end of the layer", "[sample]")
{
    // The plugin hands the engine no sample audio for a block when a sample
    // is being swapped at that very moment. The layer must carry on afterwards.
    const auto sample = fxtest::makeSineSample (3.0, 440.0);
    const auto* audio = sample.get();

    for (const auto mode : { SampleMode::resample, SampleMode::loop, SampleMode::oneShot })
    {
        INFO ("mode " << (int) mode);

        HostSim host;
        host.beforeBlock = [audio] (long long position, HostSim& sim, double&)
        {
            const bool unavailable = position >= 59 * 512 && position < 60 * 512;
            sim.sources[0].raw = unavailable ? nullptr : audio;
        };

        const auto result = render (samplePatch (mode), sample, 1.0, 3 * 48000, {}, host);

        REQUIRE (fxtest::peakBetween (result.audio, 59 * 512 + 16, 60 * 512 - 16) == 0.0f);
        REQUIRE (fxtest::peakBetween (result.audio, 62 * 512, 80000) == Catch::Approx (0.5f).epsilon (0.05));
        REQUIRE (result.last < oneBar);
    }
}

TEST_CASE ("Stretch renders are refused when they would be too short or enormous", "[sample]")
{
    const auto sample = fxtest::makeSineSample (3.0, 440.0);
    auto params = samplePatch (SampleMode::stretch).samples[0];

    REQUIRE (makeStretchJob (sample, params, 7.5).isValid());

    // 32 bars at a crawl: minutes of audio per slot.
    REQUIRE_FALSE (makeStretchJob (sample, params, 1500.0).isValid());

    // A sliver of the body: stretchSample would not render it either.
    params.start = 0.0f;
    params.end = 0.02f;
    REQUIRE_FALSE (makeStretchJob (sample, params, 0.06).isValid());
}
