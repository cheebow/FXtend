#pragma once

#include "../dsp/EnvelopeCurve.h"

#include <array>

namespace fxtend
{

inline constexpr int numSynthLayers = 4;
inline constexpr int numSampleLayers = 4;
inline constexpr int numMacros = 4;

enum class FxType { impact = 0, riser, downlifter, sweep, clap, subDrop };
inline constexpr int numFxTypes = 6;

enum class LengthUnit { bars = 0, beats, quarterNotes };
enum class RetriggerMode { restart = 0, overlap };

/** What a MIDI note does while the host is not playing. With the host playing
    a note always plays its full length, whatever this says. */
enum class StoppedNotes
{
    full = 0,   // the whole FX, like during playback
    gate,       // only for as long as the key is held
    off         // nothing: the host's note audition stays silent
};
enum class TailMode { strict = 0, natural };

enum class SynthType { noise = 0, sub, fm, transient, clap };
enum class FilterType { off = 0, lowPass, highPass, bandPass };
enum class ReverbType { hall = 0, room, plate, cathedral, dark };
enum class DelayType { pingPong = 0, straight, tape };
enum class LfoShape { sine = 0, triangle, square, sawDown, random };
enum class SampleMode { stretch = 0, resample, loop, oneShot };

/** Everything that defines one synth layer.

    Every member is a float so that the whole struct can be described by one
    table (see LayerFields.h) that drives host parameters, presets and macros.
    Choices are stored as their index; use the typed accessors to read them. */
struct SynthLayerParams
{
    float enabled = 0.0f;
    float type = 0.0f;              // SynthType
    float levelDb = -6.0f;
    float pan = 0.0f;               // -1..1
    float width = 1.0f;             // 0..1 stereo spread
    float start = 0.0f;             // portion of the body this layer plays in (0..1)
    float end = 1.0f;
    float attackMs = 0.0f;

    // Volume over the layer's own progress.
    float ampStart = 1.0f;
    float ampEnd = 1.0f;
    float ampShape = 0.0f;          // CurveShape
    float ampTension = 0.0f;

    // Pitch: baseHz shifted by a semitone curve (Sub / FM; also offsets filters).
    float baseHz = 55.0f;
    float pitchStart = 0.0f;
    float pitchEnd = 0.0f;
    float pitchShape = 0.0f;
    float pitchTension = 0.0f;

    // Filter sweep.
    float filterType = 0.0f;        // FilterType
    float cutoffStart = 1000.0f;
    float cutoffEnd = 1000.0f;
    float cutoffShape = 0.0f;
    float cutoffTension = 0.0f;
    float resonance = 0.2f;

    float variant = 0.0f;           // Noise: white/pink/brown. Sub: sine/triangle. Transient: noise/click.
    float fmRatio = 2.0f;
    float fmIndexStart = 0.0f;      // follows the filter curve shape
    float fmIndexEnd = 0.0f;

    float count = 1.0f;             // Transient / Clap: number of bursts
    float spreadMs = 12.0f;         // time between bursts
    float decayMs = 30.0f;          // burst decay
    float drive = 0.0f;

    // Measured in real time rather than against the body, so a hit keeps its
    // punch whatever the length: a pitch drop at the start of the layer, and a
    // decay (0 = off; for Clap it starts after the last burst).
    float pitchDropSt = 0.0f;
    float pitchDropMs = 40.0f;
    float ampDecayMs = 0.0f;

    // LFO: wobbles pitch, filter and volume. Its rate glides from start to end over the layer.
    float lfoShape = 0.0f;          // LfoShape
    float lfoRateStart = 6.0f;      // Hz
    float lfoRateEnd = 6.0f;
    float lfoPitchSt = 0.0f;        // depth in semitones
    float lfoCutoffOct = 0.0f;      // depth in octaves
    float lfoAmp = 0.0f;            // 0..1 tremolo depth

    // Only used when the matching shape is CurveShape::custom. Not host parameters.
    CurvePoints ampPoints = defaultCurvePoints;
    CurvePoints pitchPoints = defaultCurvePoints;
    CurvePoints cutoffPoints = defaultCurvePoints;

    bool isEnabled() const noexcept                 { return enabled >= 0.5f; }
    SynthType getType() const noexcept              { return (SynthType) (int) (type + 0.5f); }
    FilterType getFilterType() const noexcept       { return (FilterType) (int) (filterType + 0.5f); }
    int getVariant() const noexcept                 { return (int) (variant + 0.5f); }
    int getCount() const noexcept                   { return (int) (count + 0.5f); }

    float ampAt (float x) const noexcept
    {
        return ampStart + (ampEnd - ampStart) * evaluateCurve ((CurveShape) (int) (ampShape + 0.5f), ampTension, ampPoints, x);
    }

    float pitchAt (float x) const noexcept
    {
        return pitchStart + (pitchEnd - pitchStart) * evaluateCurve ((CurveShape) (int) (pitchShape + 0.5f), pitchTension, pitchPoints, x);
    }

    float filterCurveAt (float x) const noexcept
    {
        return evaluateCurve ((CurveShape) (int) (cutoffShape + 0.5f), cutoffTension, cutoffPoints, x);
    }
};

/** Everything that defines one sample layer (which file it plays is stored
    separately, see SampleSource). All floats, described by getSampleLayerFields(). */
struct SampleLayerParams
{
    float enabled = 1.0f;           // a slot only sounds once a sample is loaded
    float mode = 0.0f;              // SampleMode
    float levelDb = -6.0f;
    float pan = 0.0f;
    float width = 1.0f;
    float start = 0.0f;             // portion of the body this layer plays in (0..1)
    float end = 1.0f;

    float trimStart = 0.0f;         // portion of the file that is used (0..1)
    float trimEnd = 1.0f;
    float reverse = 0.0f;
    float fadeInMs = 2.0f;
    float fadeOutMs = 10.0f;
    float pitch = 0.0f;             // semitones; no effect in Resample mode

    float ampStart = 1.0f;          // volume over the layer's own progress
    float ampEnd = 1.0f;
    float ampShape = 0.0f;
    float ampTension = 0.0f;

    bool isEnabled() const noexcept             { return enabled >= 0.5f; }
    bool isReversed() const noexcept            { return reverse >= 0.5f; }
    SampleMode getMode() const noexcept         { return (SampleMode) (int) (mode + 0.5f); }

    float ampAt (float x) const noexcept
    {
        return ampStart + (ampEnd - ampStart) * evaluateCurve ((CurveShape) (int) (ampShape + 0.5f), ampTension, defaultCurvePoints, x);
    }
};

/** Master section. All floats, described by getMasterFields() in LayerFields.h. */
struct MasterParams
{
    float highPassHz = 20.0f;       // 20 = off
    float lowPassHz = 20000.0f;     // 20000 = off
    float tiltDb = 0.0f;            // negative = darker, positive = brighter
    float drive = 0.0f;
    float width = 1.0f;             // 0 mono .. 1 as is .. 2 extra wide

    float delayMix = 0.0f;
    float delayTime = 2.0f;         // index into the note-value list
    float delayFeedback = 0.35f;

    float reverbMix = 0.0f;
    float reverbSize = 0.6f;
    float reverbDamping = 0.4f;

    float limiter = 1.0f;           // on/off

    float reverbType = 0.0f;        // ReverbType
    float delayType = 0.0f;         // DelayType

    // Compressor on the whole output: one amount (0 = off) that lowers the
    // threshold and raises the ratio together, with make-up gain.
    float compAmount = 0.0f;
    float compAttackMs = 15.0f;     // longer lets more of the hit through before it clamps down
    float compReleaseMs = 150.0f;
    float limiterBoostDb = 0.0f;    // gain pushed into the limiter: louder and denser

    float bassMonoHz = 0.0f;        // the output is mono below this (0 = off): wide highs, solid lows

    // Imager: stereo width of the output per band (0 mono .. 1 as is .. 2 extra wide).
    float widthLow = 1.0f;
    float widthMid = 1.0f;
    float widthHigh = 1.0f;
    float imagerLowHz = 200.0f;     // where low ends and mid begins
    float imagerHighHz = 3000.0f;   // where mid ends and high begins
};

/** The complete sound: what a preset stores and what the engine plays. */
struct Patch
{
    FxType fxType = FxType::riser;
    std::array<float, numMacros> macros { 0.5f, 0.5f, 0.5f, 0.5f };    // 0.5 = neutral
    std::array<SynthLayerParams, numSynthLayers> synth;
    std::array<SampleLayerParams, numSampleLayers> samples;
    float masterGainDb = 0.0f;
    TailMode tailMode = TailMode::strict;
    MasterParams master;
};

/** The tail behaviour an FX type starts out with. */
inline TailMode getDefaultTailMode (FxType type) noexcept
{
    return (type == FxType::impact || type == FxType::clap) ? TailMode::natural : TailMode::strict;
}

} // namespace fxtend
