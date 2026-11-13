#include "TestHost.h"
#include "engine/OfflineRenderer.h"
#include "preset/FactoryPresets.h"

#include <catch2/catch_test_macros.hpp>

using namespace fxtend;
using fxtest::HostSim;

namespace
{
    std::vector<std::vector<float>> renderPreset (const Patch& patch, double sampleRate, int blockSize, long long startSample,
                                                  long long totalSamples)
    {
        HostSim host;
        host.sampleRate = sampleRate;
        host.blockSize = blockSize;
        host.events = { { startSample, true } };

        FxEngine engine;
        EngineSettings settings;
        settings.lengthValue = 1.0;
        engine.setSettings (settings);
        engine.setPatch (patch);
        engine.prepare (sampleRate, blockSize);
        return host.render (engine, totalSamples);
    }
}

TEST_CASE ("Every factory preset sounds the same at every host buffer size", "[regression]")
{
    for (const auto& preset : getFactoryPresets())
    {
        if (preset.name == emptyPresetName)
            continue;       // silent by design; see PresetTests

        INFO (preset.name);

        // Started off the block grid on purpose.
        const auto reference = renderPreset (preset.patch, 48000.0, 32, 777, 3 * 48000);

        for (const int blockSize : { 64, 333, 1024 })
        {
            const auto other = renderPreset (preset.patch, 48000.0, blockSize, 777, 3 * 48000);
            float difference = 0.0f;

            for (size_t n = 0; n < reference[0].size(); ++n)
                difference = std::max ({ difference,
                                         std::abs (reference[0][n] - other[0][n]),
                                         std::abs (reference[1][n] - other[1][n]) });

            INFO ("block size " << blockSize);
            REQUIRE (difference < 3.0e-3f);
        }

        REQUIRE (fxtest::firstAudible (reference) >= 777);
    }
}

TEST_CASE ("Every factory preset ends on the same musical position at every sample rate", "[regression]")
{
    for (const auto& preset : getFactoryPresets())
    {
        if (preset.name == emptyPresetName)
            continue;       // silent by design; see PresetTests

        Patch strict = preset.patch;
        strict.tailMode = TailMode::strict;

        for (const double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            INFO (preset.name << " at " << sampleRate << " Hz");

            const auto audio = renderPreset (strict, sampleRate, 256, 100, (long long) (2.2 * sampleRate));
            const auto expectedEnd = 100 + (long long) std::ceil (1.875 * sampleRate - 1.0e-6);

            REQUIRE (fxtest::lastAudible (audio) < expectedEnd);
            REQUIRE (fxtest::peakBetween (audio, 100, expectedEnd) > 0.03f);
            REQUIRE (fxtest::peakBetween (audio, 100, expectedEnd) <= 1.0f);
        }
    }
}

TEST_CASE ("Real-time playback and offline rendering give the same audio for every factory preset", "[regression][offline]")
{
    for (const auto& preset : getFactoryPresets())
    {
        if (preset.name == emptyPresetName)
            continue;       // silent by design; see PresetTests

        INFO (preset.name);

        RenderRequest request;
        request.patch = preset.patch;
        request.settings.lengthValue = 1.0;
        request.bpm = 128.0;
        request.sampleRate = 48000.0;
        request.velocity = 100.0f / 127.0f;

        const auto offline = renderOffline (request);
        REQUIRE (offline.completed);
        REQUIRE (offline.bodySamples == 90000);

        const auto live = renderPreset (preset.patch, 48000.0, 200, 0, offline.audio.getNumSamples());
        float difference = 0.0f;

        for (int n = 0; n < offline.audio.getNumSamples(); ++n)
            difference = std::max (difference, std::abs (offline.audio.getSample (0, n) - live[0][(size_t) n]));

        REQUIRE (difference < 3.0e-3f);
    }
}
