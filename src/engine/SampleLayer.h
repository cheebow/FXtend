#pragma once

#include "SampleData.h"

#include <cstdint>

namespace fxtend
{

/** The per-voice state of one sample layer.

    Stretch and Resample read through their audio in step with the voice
    progress, so they always arrive at the end of the region exactly at the
    layer's end, whatever the tempo does. Loop and One-shot play at the
    sample's own speed and are simply cut (with a fade) at the layer's end. */
class SampleLayer
{
public:
    static constexpr int maxChunk = 16;

    void prepare (double sampleRate) noexcept;
    void start() noexcept;

    /** Call at the start of every block, before control()/render(): the audio
        this layer was reading may have been replaced or taken away. */
    void beginBlock (const SampleSource& source) noexcept;

    /** voicePhase: 0..1 progress of the voice body.
        bodySamples: length of the whole body in samples at the current tempo. */
    void control (const SampleLayerParams& params, const SampleSource& source, double voicePhase,
                  double bodySamples, int numSamples) noexcept;

    /** Adds numSamples (<= maxChunk) to left/right. */
    void render (float* left, float* right, int numSamples) noexcept;

private:
    enum class Stage { waiting, playing, ending, done };

    void readFrame (double position, float& left, float& right) const noexcept;

    double sampleRate = 44100.0;
    Stage stage = Stage::waiting;
    std::int64_t samplesPlayed = 0;

    // What is being read, as decided at the last control point.
    const SampleData* rawInUse = nullptr;
    const SampleData* stretchedInUse = nullptr;
    const juce::AudioBuffer<float>* buffer = nullptr;
    double regionStart = 0.0;           // frames
    double regionLength = 0.0;          // frames
    bool reversed = false;
    bool phaseLocked = true;
    bool looping = false;
    double loopCrossfade = 0.0;         // frames
    bool firstLoopCycle = true;

    double position = 0.0;              // frames from regionStart
    double increment = 1.0;             // frames per output sample
    bool needsReposition = true;

    float width = 1.0f;
    float gainLeft = 0.0f, gainRight = 0.0f;
    float gainLeftStep = 0.0f, gainRightStep = 0.0f;
    float endingStep = 0.0f;
    int endingSamples = 0;
};

} // namespace fxtend
