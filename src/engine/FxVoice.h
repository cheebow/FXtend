#pragma once

#include "Patch.h"
#include "SampleLayer.h"
#include "SynthLayer.h"
#include "TransportClock.h"

#include <array>
#include <cstdint>

namespace fxtend
{

struct VoiceStart
{
    double startPpq = 0.0;      // quarter-note position of the triggering Note On
    double lengthQn = 16.0;     // body length in quarter notes
    float gain = 1.0f;          // from velocity
    int sampleOffset = 0;       // position of the Note On inside the current block
    std::uint32_t seed = 1;
};

/** One triggered FX. Its progress is measured against the musical timeline:
    phase = (ppq - startPpq) / lengthQn, and the body ends on the exact sample
    where phase reaches 1, regardless of how long the MIDI note was held. */
class FxVoice
{
public:
    void prepare (double sampleRate) noexcept;

    void start (const VoiceStart& start) noexcept;

    /** Short forced fade-out beginning at the given sample of the next rendered block. */
    void release (int sampleOffset) noexcept;

    /** Immediate stop without a fade (transport jumps, stop). */
    void kill() noexcept;

    bool isActive() const noexcept     { return state != State::idle; }
    bool isInBody() const noexcept     { return state == State::body; }
    double getPhase() const noexcept   { return phase; }
    std::uint64_t getAge() const noexcept { return age; }

    /** Adds this voice's output for the block into left/right.
        bodyGate receives this voice's end-of-body envelope (1 while sounding,
        falling to 0 on the last samples), combined with other voices by maximum. */
    void render (float* left, float* right, float* bodyGate, int numSamples, const BlockTiming& timing,
                 const Patch& patch, const SampleSources& sources) noexcept;

private:
    enum class State { idle, body, releasing };

    double sampleRate = 44100.0;
    State state = State::idle;

    double startPpq = 0.0;
    double endPpq = 0.0;
    double lengthQn = 1.0;
    double phase = 0.0;
    float gain = 1.0f;

    int pendingStartOffset = 0;
    int pendingReleaseOffset = 0;
    float releaseGain = 1.0f;
    float releaseStep = 0.0f;
    double endFadeSamples = 1.0;

    std::uint64_t age = 0;          // samples rendered since start

    std::array<SynthLayer, numSynthLayers> synthLayers;
    std::array<SampleLayer, numSampleLayers> sampleLayers;
};

} // namespace fxtend
