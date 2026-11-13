#include "TestHost.h"
#include "engine/FxTemplates.h"
#include "engine/OfflineRenderer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fxtend;
using fxtest::HostSim;

namespace
{
    constexpr long long oneBar = 90000;     // 1 bar at 128 BPM, 48 kHz

    /** An impact with plenty of reverb and delay, so a tail is unmistakable. */
    Patch wetImpact (TailMode mode)
    {
        Patch patch = makeTemplate (FxType::impact);
        patch.tailMode = mode;
        patch.master.reverbMix = 0.5f;
        patch.master.reverbSize = 0.8f;
        patch.master.delayMix = 0.4f;
        patch.master.delayFeedback = 0.5f;
        return patch;
    }

    std::vector<std::vector<float>> render (FxEngine& engine, const Patch& patch, std::vector<fxtest::MidiEvent> events,
                                            long long totalSamples, int blockSize = 512)
    {
        HostSim host;
        host.blockSize = blockSize;
        host.events = std::move (events);

        EngineSettings settings;
        settings.lengthValue = 1.0;
        engine.setSettings (settings);
        engine.setPatch (patch);
        engine.prepare (host.sampleRate, host.blockSize);
        return host.render (engine, totalSamples);
    }
}

TEST_CASE ("Strict: nothing at all sounds after the Body End, reverb and delay included", "[tail]")
{
    FxEngine engine;
    const auto audio = render (engine, wetImpact (TailMode::strict), { { 0, true } }, 4 * 48000);

    // Effects are clearly audible while the body plays...
    REQUIRE (fxtest::peakBetween (audio, oneBar - 4800, oneBar - 480) > 0.01f);

    // ...and from the end point on the output is exactly zero.
    REQUIRE (fxtest::lastAudible (audio) < oneBar);
    REQUIRE (fxtest::peakBetween (audio, oneBar, 4 * 48000) == 0.0f);
    REQUIRE (engine.isSilent());
}

TEST_CASE ("Natural: only the effect tail continues after the Body End, then dies away", "[tail]")
{
    FxEngine natural, strict;
    const auto withTail = render (natural, wetImpact (TailMode::natural), { { 0, true } }, 14 * 48000);
    const auto withoutTail = render (strict, wetImpact (TailMode::strict), { { 0, true } }, 14 * 48000);

    // Up to shortly before the end the two modes are the same sound.
    float bodyDifference = 0.0f;

    for (long long n = 0; n < oneBar - 480; ++n)
        bodyDifference = std::max (bodyDifference, std::abs (withTail[0][(size_t) n] - withoutTail[0][(size_t) n]));

    REQUIRE (bodyDifference < 1.0e-5f);

    // After the end a tail rings on, quieter than the body was...
    const float tailPeak = fxtest::peakBetween (withTail, oneBar, oneBar + 24000);
    REQUIRE (tailPeak > 1.0e-3f);
    REQUIRE (tailPeak < fxtest::peakBetween (withTail, 0, 4800));

    // ...keeps falling, and stops completely within the tail limit.
    REQUIRE (fxtest::peakBetween (withTail, oneBar + 4 * 48000, oneBar + 5 * 48000) < tailPeak);
    REQUIRE (fxtest::lastAudible (withTail) < 13 * 48000);
    REQUIRE (natural.isSilent());
}

TEST_CASE ("A Strict FX never leaks the previous trigger's effects into the next one", "[tail]")
{
    FxEngine engine;
    const long long second = 3 * 48000;
    const auto audio = render (engine, wetImpact (TailMode::strict), { { 0, true }, { second, true } }, 6 * 48000);

    // The noise layers differ per trigger, so compare levels rather than samples:
    // the second hit must start from silence, exactly like the first.
    REQUIRE (fxtest::peakBetween (audio, oneBar, second) == 0.0f);

    Patch tonal = wetImpact (TailMode::strict);
    for (auto& layer : tonal.synth)     // keep only the layers that sound the same every time
        if (layer.getType() != SynthType::sub && layer.getType() != SynthType::fm)
            layer.enabled = 0.0f;

    FxEngine tonalEngine;
    const auto repeatable = render (tonalEngine, tonal, { { 0, true }, { second, true } }, 6 * 48000);

    float difference = 0.0f;
    long long firstDifferent = -1;

    for (long long n = 0; n < oneBar; ++n)
    {
        const float d = std::abs (repeatable[0][(size_t) n] - repeatable[0][(size_t) (second + n)]);

        if (d > 1.0e-4f && firstDifferent < 0)
            firstDifferent = n;

        difference = std::max (difference, d);
    }

    INFO ("first difference at sample " << firstDifferent);
    REQUIRE (difference < 1.0e-4f);
}

TEST_CASE ("Seeking during a Natural tail leaves the tail alone but a Strict FX goes quiet", "[tail]")
{
    for (const auto mode : { TailMode::strict, TailMode::natural })
    {
        HostSim host;
        host.events = { { 0, true } };
        const long long jumpAt = 512 * 60;      // during the body
        host.beforeBlock = [] (long long position, HostSim&, double& ppq)
        {
            if (position == jumpAt)
                ppq = 64.0;
        };

        FxEngine engine;
        EngineSettings settings;
        settings.lengthValue = 1.0;
        engine.setSettings (settings);
        engine.setPatch (wetImpact (mode));
        engine.prepare (host.sampleRate, host.blockSize);

        const auto audio = host.render (engine, 3 * 48000);
        const float after = fxtest::peakBetween (audio, jumpAt, jumpAt + 4800);

        if (mode == TailMode::strict)
            REQUIRE (after == 0.0f);
        else
            REQUIRE (after > 1.0e-3f);      // the voice is cut, what already went into the reverb rings out
    }
}

TEST_CASE ("Offline render matches real-time playback and includes the tail only in Natural mode", "[tail][offline]")
{
    for (const auto mode : { TailMode::strict, TailMode::natural })
    {
        RenderRequest request;
        request.patch = wetImpact (mode);
        request.settings.lengthValue = 1.0;
        request.bpm = 128.0;
        request.sampleRate = 48000.0;
        request.velocity = 100.0f / 127.0f;

        const auto offline = renderOffline (request);
        REQUIRE (offline.completed);
        REQUIRE (offline.bodySamples == oneBar);
        REQUIRE (getBodyLengthInSamples (request) == oneBar);

        if (mode == TailMode::strict)
            REQUIRE (offline.audio.getNumSamples() == oneBar);
        else
        {
            REQUIRE (offline.audio.getNumSamples() > oneBar + 24000);
            REQUIRE (offline.audio.getNumSamples() <= oneBar + 10 * 48000);
        }

        // Same note played "live" through a host with a different buffer size.
        FxEngine engine;
        const auto live = render (engine, request.patch, { { 0, true, 60, 100 } }, offline.audio.getNumSamples(), 96);

        float difference = 0.0f;

        for (int n = 0; n < offline.audio.getNumSamples(); ++n)
            difference = std::max (difference, std::abs (offline.audio.getSample (0, n) - live[0][(size_t) n]));

        REQUIRE (difference < 2.0e-3f);
    }
}

TEST_CASE ("Master tone controls change the sound without breaking the end point", "[tail]")
{
    Patch base = fxtest::steadyNoisePatch();

    FxEngine reference;
    const auto plain = render (reference, base, { { 0, true } }, 2 * 48000);

    const auto energy = [] (const std::vector<std::vector<float>>& audio)
    {
        double sum = 0.0;

        for (long long n = 4800; n < oneBar - 4800; ++n)
            sum += (double) audio[0][(size_t) n] * audio[0][(size_t) n];

        return sum;
    };

    SECTION ("Low pass removes energy")
    {
        base.master.lowPassHz = 500.0f;
        FxEngine engine;
        const auto audio = render (engine, base, { { 0, true } }, 2 * 48000);
        REQUIRE (energy (audio) < 0.2 * energy (plain));
        REQUIRE (fxtest::lastAudible (audio) < oneBar);
    }

    SECTION ("High pass removes energy")
    {
        base.master.highPassHz = 2000.0f;
        FxEngine engine;
        const auto audio = render (engine, base, { { 0, true } }, 2 * 48000);
        REQUIRE (energy (audio) < 0.97 * energy (plain));
        REQUIRE (fxtest::lastAudible (audio) < oneBar);
    }

    SECTION ("Width 0 makes it mono")
    {
        base.master.width = 0.0f;
        FxEngine engine;
        const auto audio = render (engine, base, { { 0, true } }, 2 * 48000);

        for (long long n = 0; n < oneBar; n += 97)
            REQUIRE (audio[0][(size_t) n] == Catch::Approx (audio[1][(size_t) n]).margin (1.0e-6));
    }

    SECTION ("The limiter holds a hot signal under full scale; without it the signal clips")
    {
        base.masterGainDb = 12.0f;
        FxEngine limited;
        REQUIRE (fxtest::peakBetween (render (limited, base, { { 0, true, 60, 127 } }, 2 * 48000), 0, oneBar) <= 0.95f);

        base.master.limiter = 0.0f;
        FxEngine open;
        REQUIRE (fxtest::peakBetween (render (open, base, { { 0, true, 60, 127 } }, 2 * 48000), 0, oneBar) > 1.0f);
        REQUIRE (open.getStatus().clipCount.load() > 0);
    }
}

TEST_CASE ("Every reverb and delay type keeps the tail rules and sounds different from the others", "[tail]")
{
    std::vector<std::vector<float>> previous;

    for (int reverbType = 0; reverbType < 5; ++reverbType)
    {
        for (int delayType = 0; delayType < 3; ++delayType)
        {
            INFO ("reverb type " << reverbType << ", delay type " << delayType);

            Patch patch = wetImpact (TailMode::strict);
            patch.master.reverbType = (float) reverbType;
            patch.master.delayType = (float) delayType;

            // Strict: silent from the end point on, whatever the effect.
            FxEngine strict;
            const auto strictAudio = render (strict, patch, { { 0, true } }, 3 * 48000);
            REQUIRE (fxtest::peakBetween (strictAudio, 0, oneBar) > 0.05f);
            REQUIRE (fxtest::peakBetween (strictAudio, 0, 3 * 48000) <= 1.0f);
            REQUIRE (fxtest::peakBetween (strictAudio, oneBar, 3 * 48000) == 0.0f);

            // The same audio at another host buffer size.
            FxEngine small;
            const auto smallAudio = render (small, patch, { { 0, true } }, 3 * 48000, 64);
            float difference = 0.0f;

            for (size_t n = 0; n < strictAudio[0].size(); ++n)
                difference = std::max (difference, std::abs (strictAudio[0][n] - smallAudio[0][n]));

            REQUIRE (difference < 1.0e-4f);

            // Natural: rings on after the end and then stops.
            patch.tailMode = TailMode::natural;
            FxEngine natural;
            const auto naturalAudio = render (natural, patch, { { 0, true } }, 16 * 48000);
            REQUIRE (fxtest::peakBetween (naturalAudio, oneBar, oneBar + 24000) > 1.0e-3f);
            REQUIRE (fxtest::lastAudible (naturalAudio) < 15 * 48000);

            if (! previous.empty())
            {
                float change = 0.0f;

                for (size_t n = 0; n < strictAudio[0].size(); ++n)
                    change = std::max (change, std::abs (strictAudio[0][n] - previous[0][n]));

                REQUIRE (change > 0.01f);
            }

            previous = strictAudio;
        }
    }
}

TEST_CASE ("Compressor and limiter boost make the sound denser without breaking the peak or the end point", "[tail][dynamics]")
{
    const auto rmsOf = [] (const std::vector<std::vector<float>>& audio, long long from, long long to)
    {
        double sum = 0.0;

        for (long long n = from; n < to; ++n)
            sum += (double) audio[0][(size_t) n] * audio[0][(size_t) n];

        return (float) std::sqrt (sum / (double) (to - from));
    };

    // Turned down so that the limiter is not already working on the plain sound.
    Patch plain = wetImpact (TailMode::strict);
    plain.masterGainDb = -12.0f;
    plain.master.compAmount = 0.0f;
    plain.master.limiterBoostDb = 0.0f;
    FxEngine plainEngine;
    const auto plainAudio = render (plainEngine, plain, { { 0, true } }, 3 * 48000);
    const float plainRms = rmsOf (plainAudio, 0, oneBar);

    for (int variant = 0; variant < 3; ++variant)
    {
        INFO ("variant " << variant);
        Patch patch = plain;
        patch.master.compAmount = variant == 1 ? 0.0f : 0.7f;
        patch.master.limiterBoostDb = variant == 0 ? 0.0f : 9.0f;

        FxEngine engine;
        const auto audio = render (engine, patch, { { 0, true } }, 3 * 48000);

        // Denser (more level for the same ceiling), never over full scale, silent after the end.
        REQUIRE (rmsOf (audio, 0, oneBar) > plainRms * 1.25f);
        REQUIRE (fxtest::peakBetween (audio, 0, 3 * 48000) <= 1.0f);
        REQUIRE (fxtest::peakBetween (audio, oneBar, 3 * 48000) == 0.0f);

        // The same at another host buffer size.
        FxEngine small;
        const auto smallAudio = render (small, patch, { { 0, true } }, 3 * 48000, 64);
        float difference = 0.0f;

        for (size_t n = 0; n < audio[0].size(); ++n)
            difference = std::max (difference, std::abs (audio[0][n] - smallAudio[0][n]));

        REQUIRE (difference < 1.0e-4f);

        // Natural: the tail still dies away.
        patch.tailMode = TailMode::natural;
        FxEngine natural;
        const auto naturalAudio = render (natural, patch, { { 0, true } }, 20 * 48000);
        REQUIRE (fxtest::peakBetween (naturalAudio, oneBar, oneBar + 24000) > 1.0e-3f);
        REQUIRE (fxtest::lastAudible (naturalAudio) < 19 * 48000);
    }
}

TEST_CASE ("Bass Mono removes the stereo difference in the lows and leaves the highs wide", "[tail][stereo]")
{
    // Left only: a low tone and a high tone, so both are fully "wide" going in.
    const auto sideLevel = [] (float toneHz, float bassMonoHz)
    {
        MasterFx fx;
        MasterParams params;
        params.bassMonoHz = bassMonoHz;
        params.limiter = 0.0f;
        fx.prepare (48000.0, 512, params);

        std::vector<float> gate (512, 1.0f);
        double sum = 0.0;
        int counted = 0;

        for (int block = 0; block < 60; ++block)
        {
            std::vector<float> left (512), right (512, 0.0f);

            for (int n = 0; n < 512; ++n)
                left[(size_t) n] = 0.5f * std::sin (6.2831853f * toneHz * (float) (block * 512 + n) / 48000.0f);

            fx.process (left.data(), right.data(), 512, params, 0.0f, 120.0, gate.data(), false);

            if (block >= 30)
                for (int n = 0; n < 512; ++n, ++counted)
                    sum += std::pow (0.5 * (left[(size_t) n] - right[(size_t) n]), 2.0);
        }

        return (float) std::sqrt (sum / counted);
    };

    // Off: nothing changes.
    REQUIRE (sideLevel (50.0f, 0.0f) == Catch::Approx (sideLevel (5000.0f, 0.0f)).margin (0.01));

    // 200 Hz: a 50 Hz tone loses most of its side signal, a 5 kHz tone keeps it.
    REQUIRE (sideLevel (50.0f, 200.0f) < 0.35f * sideLevel (50.0f, 0.0f));
    REQUIRE (sideLevel (5000.0f, 200.0f) > 0.95f * sideLevel (5000.0f, 0.0f));
}

TEST_CASE ("The imager changes the width of one band and leaves the others alone", "[tail][stereo]")
{
    // A tone in the left channel only; returns the level of left minus right.
    const auto sideLevel = [] (float toneHz, const MasterParams& params)
    {
        MasterFx fx;
        fx.prepare (48000.0, 512, params);

        std::vector<float> gate (512, 1.0f);
        double sum = 0.0;
        int counted = 0;

        for (int block = 0; block < 60; ++block)
        {
            std::vector<float> left (512), right (512, 0.0f);

            for (int n = 0; n < 512; ++n)
                left[(size_t) n] = 0.25f * std::sin (6.2831853f * toneHz * (float) (block * 512 + n) / 48000.0f);

            fx.process (left.data(), right.data(), 512, params, 0.0f, 120.0, gate.data(), false);

            if (block >= 30)
                for (int n = 0; n < 512; ++n, ++counted)
                    sum += std::pow (0.5 * (left[(size_t) n] - right[(size_t) n]), 2.0);
        }

        return (float) std::sqrt (sum / counted);
    };

    MasterParams plain;
    plain.limiter = 0.0f;
    const float low = sideLevel (40.0f, plain), mid = sideLevel (800.0f, plain), high = sideLevel (12000.0f, plain);

    // All three at 1: nothing is touched, whatever the frequency.
    REQUIRE (low == Catch::Approx (mid).margin (0.002));
    REQUIRE (low == Catch::Approx (high).margin (0.002));

    MasterParams narrowLow = plain;
    narrowLow.widthLow = 0.0f;
    REQUIRE (sideLevel (40.0f, narrowLow) < 0.3f * low);
    REQUIRE (sideLevel (12000.0f, narrowLow) > 0.97f * high);

    MasterParams wideHigh = plain;
    wideHigh.widthHigh = 2.0f;
    REQUIRE (sideLevel (12000.0f, wideHigh) > 1.7f * high);
    REQUIRE (sideLevel (40.0f, wideHigh) < 1.05f * low);

    MasterParams narrowMid = plain;
    narrowMid.widthMid = 0.0f;
    REQUIRE (sideLevel (800.0f, narrowMid) < 0.3f * mid);
    REQUIRE (sideLevel (40.0f, narrowMid) > 0.9f * low);
    REQUIRE (sideLevel (12000.0f, narrowMid) > 0.9f * high);
}

TEST_CASE ("Width 0 makes the whole output mono, reverb and delay included", "[tail][stereo]")
{
    Patch patch = wetImpact (TailMode::natural);
    patch.master.width = 0.0f;

    FxEngine engine;
    const auto audio = render (engine, patch, { { 0, true } }, 6 * 48000);
    REQUIRE (fxtest::peakBetween (audio, oneBar, oneBar + 24000) > 1.0e-3f);     // the tail is there

    float difference = 0.0f;

    for (size_t n = 0; n < audio[0].size(); ++n)
        difference = std::max (difference, std::abs (audio[0][n] - audio[1][n]));

    REQUIRE (difference < 1.0e-6f);
}
