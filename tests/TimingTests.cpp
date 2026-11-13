#include "TestHost.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace fxtend;
using fxtest::HostSim;

namespace
{
    void prepare (FxEngine& engine, const HostSim& host, double bars = 4.0)
    {
        EngineSettings settings;
        settings.lengthUnit = LengthUnit::bars;
        settings.lengthValue = bars;
        engine.setSettings (settings);
        engine.setPatch (fxtest::steadyNoisePatch());
        engine.prepare (host.sampleRate, host.blockSize);
    }

    // The last samples sit at the bottom of the end fade and may round to
    // zero, but the body must never run past the end point.
    void requireEndsAt (const std::vector<std::vector<float>>& audio, long long expectedEnd)
    {
        const long long last = fxtest::lastAudible (audio);
        REQUIRE (last < expectedEnd);
        REQUIRE (last >= expectedEnd - 4);
    }
}

TEST_CASE ("Length is measured in the host's musical units", "[timing]")
{
    REQUIRE (FxEngine::lengthInQuarterNotes (LengthUnit::bars, 4.0, { 4, 4 }) == Catch::Approx (16.0));
    REQUIRE (FxEngine::lengthInQuarterNotes (LengthUnit::bars, 1.0, { 6, 8 }) == Catch::Approx (3.0));
    REQUIRE (FxEngine::lengthInQuarterNotes (LengthUnit::bars, 0.5, { 3, 4 }) == Catch::Approx (1.5));
    REQUIRE (FxEngine::lengthInQuarterNotes (LengthUnit::beats, 1.0, { 4, 4 }) == Catch::Approx (1.0));
    REQUIRE (FxEngine::lengthInQuarterNotes (LengthUnit::beats, 1.0, { 6, 8 }) == Catch::Approx (0.5));
    REQUIRE (FxEngine::lengthInQuarterNotes (LengthUnit::quarterNotes, 7.0, { 6, 8 }) == Catch::Approx (7.0));

    // Clamped to 0.25 quarter notes .. 32 bars.
    REQUIRE (FxEngine::lengthInQuarterNotes (LengthUnit::quarterNotes, 0.01, { 4, 4 }) == Catch::Approx (0.25));
    REQUIRE (FxEngine::lengthInQuarterNotes (LengthUnit::bars, 100.0, { 4, 4 }) == Catch::Approx (128.0));
}

TEST_CASE ("A short note still plays the full 4 bars and stops at 7.5 s (128 BPM)", "[timing]")
{
    HostSim host;
    host.events = { { 0, true }, { 2400, false } };     // 50 ms note

    FxEngine engine;
    prepare (engine, host);

    const auto audio = host.render (engine, 9 * 48000);

    REQUIRE (fxtest::firstAudible (audio) <= 1);
    requireEndsAt (audio, 360000);                      // 7.5 s * 48 kHz

    // Still sounding well after the Note Off and right up to the end.
    REQUIRE (fxtest::peakBetween (audio, 100000, 101000) > 0.01f);
    REQUIRE (fxtest::peakBetween (audio, 358000, 359000) > 0.01f);
}

TEST_CASE ("Note On sample offsets inside a block are honoured", "[timing]")
{
    const int offset = GENERATE (1, 137, 511);

    HostSim host;
    host.events = { { 5 * 512 + offset, true }, { 5 * 512 + offset + 100, false } };

    FxEngine engine;
    prepare (engine, host, 1.0);

    const auto audio = host.render (engine, 3 * 48000);
    const long long start = 5 * 512 + offset;

    const long long first = fxtest::firstAudible (audio);
    REQUIRE (first >= start);
    REQUIRE (first <= start + 1);
    requireEndsAt (audio, start + 90000);               // 1 bar at 128 BPM = 1.875 s
}

TEST_CASE ("Changing the tempo changes the length in seconds, not in bars", "[timing]")
{
    HostSim host;
    host.bpmAt = [] (long long) { return 100.0; };
    host.events = { { 0, true }, { 1000, false } };

    FxEngine engine;
    prepare (engine, host);

    const auto audio = host.render (engine, 11 * 48000);
    requireEndsAt (audio, 460800);                      // 9.6 s * 48 kHz
}

TEST_CASE ("A tempo change during the body keeps the musical end point", "[timing]")
{
    HostSim host;
    host.blockSize = 256;
    const long long changeAt = 256 * 400;               // tempo drops after ~2.13 s
    host.bpmAt = [] (long long position) { return position < changeAt ? 128.0 : 100.0; };
    host.events = { { 0, true } };

    FxEngine engine;
    prepare (engine, host);

    const auto audio = host.render (engine, 11 * 48000);

    // 16 quarter notes: the first part at 128 BPM, the rest at 100 BPM.
    const double qnBeforeChange = changeAt * 128.0 / (60.0 * 48000.0);
    const double expectedEnd = changeAt + (16.0 - qnBeforeChange) * 60.0 * 48000.0 / 100.0;

    const long long last = fxtest::lastAudible (audio);
    REQUIRE (last < (long long) std::ceil (expectedEnd));
    REQUIRE (last >= (long long) std::ceil (expectedEnd) - 4);
}

TEST_CASE ("A tempo ramp still ends on the bar line the host reports", "[timing]")
{
    HostSim host;
    host.blockSize = 480;
    host.bpmAt = [] (long long position) { return 100.0 + 40.0 * std::min (1.0, (double) position / (48000.0 * 6.0)); };
    host.events = { { 0, true } };

    FxEngine engine;
    prepare (engine, host, 2.0);

    // Find where the simulated host reaches 8 quarter notes.
    double ppq = 0.0;
    long long expectedEnd = 0;

    for (long long position = 0;; position += host.blockSize)
    {
        const double step = host.bpmAt (position) / (60.0 * host.sampleRate);

        if (ppq + step * host.blockSize >= 8.0)
        {
            expectedEnd = position + (long long) std::ceil ((8.0 - ppq) / step - 1.0e-6);
            break;
        }

        ppq += step * host.blockSize;
    }

    const auto audio = host.render (engine, 6 * 48000);
    requireEndsAt (audio, expectedEnd);
}

TEST_CASE ("End point is the same for every buffer size and sample rate", "[timing]")
{
    const double sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0);
    const int blockSize = GENERATE (32, 64, 127, 256, 1024);

    HostSim host;
    host.sampleRate = sampleRate;
    host.blockSize = blockSize;
    host.events = { { 1000, true } };

    FxEngine engine;
    prepare (engine, host, 1.0);

    const auto audio = host.render (engine, (long long) (sampleRate * 2.5));
    const long long expectedEnd = 1000 + (long long) std::llround (1.875 * sampleRate);

    REQUIRE (fxtest::firstAudible (audio) >= 1000);
    REQUIRE (fxtest::firstAudible (audio) <= 1001);
    requireEndsAt (audio, expectedEnd);
}

TEST_CASE ("Output does not depend on the buffer size", "[timing]")
{
    auto renderWith = [] (int blockSize)
    {
        HostSim host;
        host.blockSize = blockSize;
        host.events = { { 300, true } };

        FxEngine engine;
        prepare (engine, host, 1.0);
        return host.render (engine, 2 * 48000);
    };

    const auto reference = renderWith (32);

    for (int blockSize : { 100, 512, 1024 })
    {
        const auto other = renderWith (blockSize);
        float maxDifference = 0.0f;

        for (size_t n = 0; n < reference[0].size(); ++n)
            maxDifference = std::max (maxDifference, std::abs (reference[0][n] - other[0][n]));

        INFO ("block size " << blockSize);
        REQUIRE (maxDifference < 1.0e-3f);
    }
}

TEST_CASE ("Seeking or looping the host clears the sounding voice", "[timing]")
{
    HostSim host;
    host.events = { { 0, true } };

    const long long jumpAt = 512 * 100;
    host.beforeBlock = [] (long long position, HostSim&, double& ppq)
    {
        if (position == jumpAt)
            ppq = 0.0;      // loop back to the start
    };

    FxEngine engine;
    prepare (engine, host);

    const auto audio = host.render (engine, 3 * 48000);

    REQUIRE (fxtest::peakBetween (audio, jumpAt - 2000, jumpAt) > 0.01f);
    REQUIRE (fxtest::lastAudible (audio) < jumpAt);
}

TEST_CASE ("A Note On at the loop start retriggers after the wrap", "[timing]")
{
    HostSim host;
    const long long jumpAt = 512 * 100;
    host.events = { { 0, true }, { jumpAt, true } };
    host.beforeBlock = [] (long long position, HostSim&, double& ppq)
    {
        if (position == jumpAt)
            ppq = 0.0;
    };

    FxEngine engine;
    prepare (engine, host, 1.0);

    const auto audio = host.render (engine, 4 * 48000);
    requireEndsAt (audio, jumpAt + 90000);
}

TEST_CASE ("Stopping the host stops the sounding voice", "[timing]")
{
    HostSim host;
    host.events = { { 0, true } };

    const long long stopAt = 512 * 80;
    host.beforeBlock = [] (long long position, HostSim& h, double&)
    {
        if (position == stopAt)
            h.playing = false;
    };

    FxEngine engine;
    prepare (engine, host);

    const auto audio = host.render (engine, 2 * 48000);
    REQUIRE (fxtest::lastAudible (audio) < stopAt);
}

TEST_CASE ("While stopped, notes play against the manual tempo", "[timing]")
{
    HostSim host;
    host.playing = false;
    host.provideBpm = false;
    host.providePpq = false;
    host.provideTimeSig = false;
    host.events = { { 700, true } };

    FxEngine engine;
    EngineSettings settings;
    settings.lengthUnit = LengthUnit::bars;
    settings.lengthValue = 1.0;
    settings.manualBpm = 120.0;
    engine.setSettings (settings);
    engine.setPatch (fxtest::steadyNoisePatch());
    engine.prepare (host.sampleRate, host.blockSize);

    const auto audio = host.render (engine, 3 * 48000);

    REQUIRE (fxtest::firstAudible (audio) >= 700);
    requireEndsAt (audio, 700 + 96000);                 // 1 bar at 120 BPM = 2 s
    REQUIRE_FALSE (engine.getStatus().tempoFromHost.load());
    REQUIRE_FALSE (engine.getStatus().timeSigFromHost.load());
}

TEST_CASE ("Restart mode fades the previous voice; overlap mode keeps it", "[timing]")
{
    const bool overlap = GENERATE (false, true);

    HostSim host;
    host.events = { { 0, true }, { 24000, true } };

    FxEngine engine;
    EngineSettings settings;
    settings.lengthValue = 1.0;
    settings.retrigger = overlap ? RetriggerMode::overlap : RetriggerMode::restart;
    engine.setSettings (settings);
    engine.setPatch (fxtest::steadyNoisePatch());
    engine.prepare (host.sampleRate, host.blockSize);

    const auto audio = host.render (engine, 3 * 48000);

    // The second trigger always runs its full bar.
    requireEndsAt (audio, 24000 + 90000);

    host.blockSize = 512;
    engine.prepare (host.sampleRate, host.blockSize);
    host.render (engine, 24000 + 4800);

    REQUIRE (engine.getStatus().activeVoices.load() == (overlap ? 2 : 1));
}

TEST_CASE ("Rapid retriggers never run out of voices or blow up", "[timing]")
{
    const bool overlap = GENERATE (false, true);

    HostSim host;
    host.blockSize = 64;

    for (int i = 0; i < 200; ++i)
        host.events.push_back ({ i * 37, true });

    FxEngine engine;
    EngineSettings settings;
    settings.lengthValue = 1.0;
    settings.retrigger = overlap ? RetriggerMode::overlap : RetriggerMode::restart;
    engine.setSettings (settings);
    engine.setPatch (fxtest::steadyNoisePatch());
    engine.prepare (host.sampleRate, host.blockSize);

    const auto audio = host.render (engine, 3 * 48000);

    REQUIRE (engine.getStatus().noteOnCount.load() == 200);
    REQUIRE (fxtest::peakBetween (audio, 0, 3 * 48000) < 8.0f);
    requireEndsAt (audio, 199 * 37 + 90000);
}
