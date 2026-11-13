#pragma once

#include "engine/FxEngine.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace fxtest
{

struct MidiEvent
{
    long long sample = 0;       // absolute position in the render
    bool noteOn = true;
    int note = 60;
    int velocity = 100;
};

/** A minimal fake DAW: feeds the engine block by block with a transport that
    it advances itself, so tests can check sample-exact start and end points. */
struct HostSim
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool playing = true;
    bool providePpq = true;
    bool provideBpm = true;
    bool provideTimeSig = true;
    fxtend::TimeSignature timeSig { 4, 4 };
    double startPpq = 0.0;

    /** Tempo in effect for the block that starts at the given sample. */
    std::function<double (long long)> bpmAt = [] (long long) { return 128.0; };

    /** Optional: lets a test move the playhead or stop the transport. Called before each block. */
    std::function<void (long long, HostSim&, double& ppq)> beforeBlock;

    std::vector<MidiEvent> events;

    /** Audio for the sample layers, handed to the engine before every block. */
    fxtend::SampleSources sources;

    /** Renders totalSamples and returns interleaved-free stereo: result[channel][sample]. */
    std::vector<std::vector<float>> render (fxtend::FxEngine& engine, long long totalSamples)
    {
        std::vector<std::vector<float>> output (2, std::vector<float> ((size_t) totalSamples, 0.0f));
        juce::AudioBuffer<float> buffer (2, blockSize);
        double ppq = startPpq;

        for (long long position = 0; position < totalSamples; position += blockSize)
        {
            const int numSamples = (int) std::min<long long> (blockSize, totalSamples - position);

            if (beforeBlock)
                beforeBlock (position, *this, ppq);

            const double bpm = bpmAt (position);

            fxtend::TransportState state;
            state.isPlaying = playing;
            state.hasPpq = providePpq;
            state.ppq = ppq;
            state.hasBpm = provideBpm;
            state.bpm = bpm;
            state.hasTimeSig = provideTimeSig;
            state.timeSig = timeSig;

            juce::MidiBuffer midi;

            for (const auto& event : events)
            {
                if (event.sample >= position && event.sample < position + numSamples)
                {
                    const int offset = (int) (event.sample - position);
                    midi.addEvent (event.noteOn ? juce::MidiMessage::noteOn (1, event.note, (juce::uint8) event.velocity)
                                                : juce::MidiMessage::noteOff (1, event.note),
                                   offset);
                }
            }

            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, numSamples);
            engine.setSampleSources (sources);
            engine.process (view, midi, state);

            for (int channel = 0; channel < 2; ++channel)
                std::copy (view.getReadPointer (channel), view.getReadPointer (channel) + numSamples,
                           output[(size_t) channel].begin() + position);

            if (playing)
                ppq += numSamples * bpm / (60.0 * sampleRate);
        }

        return output;
    }
};

/** A patch that is plainly audible from its first to its last sample. */
inline fxtend::Patch steadyNoisePatch()
{
    fxtend::Patch patch;
    patch.synth[0].enabled = 1.0f;
    patch.synth[0].type = (float) (int) fxtend::SynthType::noise;
    patch.synth[0].levelDb = -6.0f;
    return patch;
}

/** Estimates the frequency of a steady tone from its zero crossings. */
inline double estimateFrequency (const std::vector<float>& signal, long long from, long long to, double sampleRate)
{
    int crossings = 0;
    long long firstCrossing = -1, lastCrossing = -1;

    for (long long n = from + 1; n < to; ++n)
    {
        if (signal[(size_t) n - 1] < 0.0f && signal[(size_t) n] >= 0.0f)
        {
            if (firstCrossing < 0)
                firstCrossing = n;

            lastCrossing = n;
            ++crossings;
        }
    }

    return crossings > 1 ? (crossings - 1) * sampleRate / (double) (lastCrossing - firstCrossing) : 0.0;
}

/** A sine tone as a sample, with an optional linear fade from startLevel to endLevel. */
inline fxtend::SampleData::Ptr makeSineSample (double seconds, double frequency, double sampleRate = 44100.0,
                                                float startLevel = 0.5f, float endLevel = 0.5f)
{
    fxtend::SampleData::Ptr sample (new fxtend::SampleData());
    const int frames = (int) (seconds * sampleRate);
    sample->sampleRate = sampleRate;
    sample->audio.setSize (2, frames);

    for (int n = 0; n < frames; ++n)
    {
        const float level = startLevel + (endLevel - startLevel) * (float) n / (float) frames;
        const float value = level * (float) std::sin (6.283185307179586 * frequency * n / sampleRate);
        sample->audio.setSample (0, n, value);
        sample->audio.setSample (1, n, value);
    }

    return sample;
}

inline long long firstAudible (const std::vector<std::vector<float>>& audio, float threshold = 0.0f)
{
    for (size_t n = 0; n < audio[0].size(); ++n)
        if (std::abs (audio[0][n]) > threshold || std::abs (audio[1][n]) > threshold)
            return (long long) n;

    return -1;
}

inline long long lastAudible (const std::vector<std::vector<float>>& audio, float threshold = 0.0f)
{
    for (size_t n = audio[0].size(); n-- > 0;)
        if (std::abs (audio[0][n]) > threshold || std::abs (audio[1][n]) > threshold)
            return (long long) n;

    return -1;
}

inline float peakBetween (const std::vector<std::vector<float>>& audio, long long from, long long to)
{
    float peak = 0.0f;

    for (long long n = std::max<long long> (0, from); n < std::min<long long> (to, (long long) audio[0].size()); ++n)
        peak = std::max ({ peak, std::abs (audio[0][(size_t) n]), std::abs (audio[1][(size_t) n]) });

    return peak;
}

} // namespace fxtest
