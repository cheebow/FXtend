#pragma once

#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace fxtest
{

/** A private folder for presets and managed samples, removed again afterwards,
    so tests never touch the real user folders. */
struct TempStorage
{
    TempStorage()
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("fxtend-tests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
    {
        root.createDirectory();
    }

    ~TempStorage() { root.deleteRecursively(); }

    juce::File root;
};

/** A playhead the test moves by hand, like a DAW transport. */
struct FakePlayHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }

    bool playing = true;
    double bpm = 128.0;
    double ppq = 0.0;
};

inline void setParameter (FXtendAudioProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.getParameters().getParameter (id);
    REQUIRE (parameter != nullptr);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

struct Playback
{
    std::vector<float> left;
    long long lastAudible = -1;
};

/** Runs the processor like a host would, with one short note at the start. */
inline Playback playNote (FXtendAudioProcessor& processor, FakePlayHead& playHead, int blockSize, long long totalSamples,
                          double sampleRate = 48000.0, bool sendNote = true)
{
    processor.setPlayHead (&playHead);
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    Playback result;
    result.left.reserve ((size_t) totalSamples);
    playHead.ppq = 0.0;

    for (long long position = 0; position < totalSamples; position += blockSize)
    {
        juce::MidiBuffer midi;

        if (position == 0 && sendNote)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), blockSize / 2);
        }

        buffer.clear();
        processor.processBlock (buffer, midi);

        for (int n = 0; n < blockSize; ++n)
        {
            result.left.push_back (buffer.getSample (0, n));

            if (std::abs (buffer.getSample (0, n)) > 1.0e-6f || std::abs (buffer.getSample (1, n)) > 1.0e-6f)
                result.lastAudible = position + n;
        }

        playHead.ppq += blockSize * playHead.bpm / (60.0 * sampleRate);
    }

    processor.setPlayHead (nullptr);
    return result;
}

inline long long playOneNote (FXtendAudioProcessor& processor, FakePlayHead& playHead, int blockSize, long long totalSamples,
                              double sampleRate = 48000.0)
{
    return playNote (processor, playHead, blockSize, totalSamples, sampleRate).lastAudible;
}

/** Writes a sine tone to a WAV or AIFF file (chosen by the extension). */
inline void writeSineFile (const juce::File& file, double seconds, double frequency, double sampleRate = 44100.0,
                           int numChannels = 2, int bitsPerSample = 24)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();

    const int frames = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> buffer (numChannels, frames);

    for (int channel = 0; channel < numChannels; ++channel)
        for (int n = 0; n < frames; ++n)
            buffer.setSample (channel, n, 0.5f * (float) std::sin (6.283185307179586 * frequency * n / sampleRate));

    std::unique_ptr<juce::AudioFormat> format;

    if (file.hasFileExtension ("aif;aiff"))
        format = std::make_unique<juce::AiffAudioFormat>();
    else
        format = std::make_unique<juce::WavAudioFormat>();

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    REQUIRE (stream != nullptr);

    auto writer = format->createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                 .withNumChannels (numChannels)
                                                                                 .withBitsPerSample (bitsPerSample));
    REQUIRE (writer != nullptr);
    REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, frames));
}

} // namespace fxtest
