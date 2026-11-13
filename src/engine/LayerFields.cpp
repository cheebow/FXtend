#include "LayerFields.h"

namespace fxtend
{

namespace
{
    using K = LayerField::Kind;
    using P = SynthLayerParams;

    constexpr const char* curveChoices = "Linear|Exponential|S-Curve|Custom";

    constexpr LayerField fields[] =
    {
        { "on",          "On",            &P::enabled,       K::toggle,     0.0f,    1.0f,     0.0f,    "",    "" },
        { "type",        "Type",          &P::type,          K::choice,     0.0f,    4.0f,     0.0f,    "",    "Noise|Sub|FM|Transient|Clap" },
        { "level",       "Level",         &P::levelDb,       K::continuous, -60.0f,  12.0f,    0.0f,    "dB",  "" },
        { "pan",         "Pan",           &P::pan,           K::continuous, -1.0f,   1.0f,     0.0f,    "",    "" },
        { "width",       "Width",         &P::width,         K::continuous, 0.0f,    1.0f,     0.0f,    "",    "" },
        { "start",       "Start",         &P::start,         K::continuous, 0.0f,    1.0f,     0.0f,    "",    "" },
        { "end",         "End",           &P::end,           K::continuous, 0.0f,    1.0f,     0.0f,    "",    "" },
        { "attack",      "Attack",        &P::attackMs,      K::continuous, 0.0f,    2000.0f,  100.0f,  "ms",  "" },

        { "ampStart",    "Amp Start",     &P::ampStart,      K::continuous, 0.0f,    1.0f,     0.0f,    "",    "" },
        { "ampEnd",      "Amp End",       &P::ampEnd,        K::continuous, 0.0f,    1.0f,     0.0f,    "",    "" },
        { "ampShape",    "Amp Curve",     &P::ampShape,      K::choice,     0.0f,    3.0f,     0.0f,    "",    curveChoices },
        { "ampTension",  "Amp Tension",   &P::ampTension,    K::continuous, -1.0f,   1.0f,     0.0f,    "",    "" },

        { "baseHz",      "Frequency",     &P::baseHz,        K::continuous, 20.0f,   4000.0f,  200.0f,  "Hz",  "" },
        { "pitchStart",  "Pitch Start",   &P::pitchStart,    K::continuous, -48.0f,  48.0f,    0.0f,    "st",  "" },
        { "pitchEnd",    "Pitch End",     &P::pitchEnd,      K::continuous, -48.0f,  48.0f,    0.0f,    "st",  "" },
        { "pitchShape",  "Pitch Curve",   &P::pitchShape,    K::choice,     0.0f,    3.0f,     0.0f,    "",    curveChoices },
        { "pitchTension","Pitch Tension", &P::pitchTension,  K::continuous, -1.0f,   1.0f,     0.0f,    "",    "" },

        { "filter",      "Filter",        &P::filterType,    K::choice,     0.0f,    3.0f,     0.0f,    "",    "Off|Low Pass|High Pass|Band Pass" },
        { "cutoffStart", "Cutoff Start",  &P::cutoffStart,   K::continuous, 20.0f,   20000.0f, 1000.0f, "Hz",  "" },
        { "cutoffEnd",   "Cutoff End",    &P::cutoffEnd,     K::continuous, 20.0f,   20000.0f, 1000.0f, "Hz",  "" },
        { "cutoffShape", "Filter Curve",  &P::cutoffShape,   K::choice,     0.0f,    3.0f,     0.0f,    "",    curveChoices },
        { "cutoffTension","Filter Tension",&P::cutoffTension, K::continuous, -1.0f,  1.0f,     0.0f,    "",    "" },
        { "resonance",   "Resonance",     &P::resonance,     K::continuous, 0.0f,    1.0f,     0.0f,    "",    "" },

        { "variant",     "Variant",       &P::variant,       K::integer,    0.0f,    2.0f,     0.0f,    "",    "" },
        { "fmRatio",     "FM Ratio",      &P::fmRatio,       K::continuous, 0.25f,   16.0f,    2.0f,    "",    "" },
        { "fmIndexStart","FM Start",      &P::fmIndexStart,  K::continuous, 0.0f,    12.0f,    0.0f,    "",    "" },
        { "fmIndexEnd",  "FM End",        &P::fmIndexEnd,    K::continuous, 0.0f,    12.0f,    0.0f,    "",    "" },

        { "count",       "Bursts",        &P::count,         K::integer,    1.0f,    8.0f,     0.0f,    "",    "" },
        { "spread",      "Spread",        &P::spreadMs,      K::continuous, 1.0f,    200.0f,   20.0f,   "ms",  "" },
        { "decay",       "Decay",         &P::decayMs,       K::continuous, 1.0f,    2000.0f,  60.0f,   "ms",  "" },
        { "drive",       "Drive",         &P::drive,         K::continuous, 0.0f,    1.0f,     0.0f,    "",    "" },

        { "pitchDrop",   "Punch Pitch",   &P::pitchDropSt,   K::continuous, 0.0f,    60.0f,    0.0f,    "st",  "" },
        { "pitchDropMs", "Punch Time",    &P::pitchDropMs,   K::continuous, 1.0f,    500.0f,   60.0f,   "ms",  "" },
        { "ampDecay",    "Amp Decay",     &P::ampDecayMs,    K::continuous, 0.0f,    5000.0f,  300.0f,  "ms",  "" },

        { "lfoShape",    "LFO Shape",     &P::lfoShape,      K::choice,     0.0f,    4.0f,     0.0f,    "",    "Sine|Triangle|Square|Saw Down|Random" },
        { "lfoRateStart","LFO Rate Start",&P::lfoRateStart,  K::continuous, 0.1f,    40.0f,    6.0f,    "Hz",  "" },
        { "lfoRateEnd",  "LFO Rate End",  &P::lfoRateEnd,    K::continuous, 0.1f,    40.0f,    6.0f,    "Hz",  "" },
        { "lfoPitch",    "LFO > Pitch",   &P::lfoPitchSt,    K::continuous, 0.0f,    24.0f,    0.0f,    "st",  "" },
        { "lfoCutoff",   "LFO > Filter",  &P::lfoCutoffOct,  K::continuous, 0.0f,    4.0f,     0.0f,    "oct", "" },
        { "lfoAmp",      "LFO > Volume",  &P::lfoAmp,        K::continuous, 0.0f,    1.0f,     0.0f,    "",    "" },
    };

    using MK = MasterField::Kind;
    using M = MasterParams;

    constexpr MasterField masterFields[] =
    {
        { "highPass",     "High Pass",     &M::highPassHz,    MK::continuous, 20.0f,   2000.0f,  200.0f,  "Hz", "" },
        { "lowPass",      "Low Pass",      &M::lowPassHz,     MK::continuous, 200.0f,  20000.0f, 4000.0f, "Hz", "" },
        { "tilt",         "Tilt",          &M::tiltDb,        MK::continuous, -6.0f,   6.0f,     0.0f,    "dB", "" },
        { "drive",        "Drive",         &M::drive,         MK::continuous, 0.0f,    1.0f,     0.0f,    "",   "" },
        { "width",        "Width",         &M::width,         MK::continuous, 0.0f,    2.0f,     0.0f,    "",   "" },
        { "delayMix",     "Delay Mix",     &M::delayMix,      MK::continuous, 0.0f,    1.0f,     0.0f,    "",   "" },
        { "delayTime",    "Delay Time",    &M::delayTime,     MK::choice,     0.0f,    5.0f,     0.0f,    "",   delayTimeChoices },
        { "delayFeedback","Delay Feedback",&M::delayFeedback, MK::continuous, 0.0f,    0.9f,     0.0f,    "",   "" },
        { "reverbMix",    "Reverb Mix",    &M::reverbMix,     MK::continuous, 0.0f,    1.0f,     0.0f,    "",   "" },
        { "reverbSize",   "Reverb Size",   &M::reverbSize,    MK::continuous, 0.0f,    1.0f,     0.0f,    "",   "" },
        { "reverbDamping","Reverb Damping",&M::reverbDamping, MK::continuous, 0.0f,    1.0f,     0.0f,    "",   "" },
        { "limiter",      "Limiter",       &M::limiter,       MK::toggle,     0.0f,    1.0f,     0.0f,    "",   "" },
        { "reverbType",   "Reverb",        &M::reverbType,    MK::choice,     0.0f,    4.0f,     0.0f,    "",   "Hall|Room|Plate|Cathedral|Dark" },
        { "delayType",    "Delay",         &M::delayType,     MK::choice,     0.0f,    2.0f,     0.0f,    "",   "Ping-Pong|Straight|Tape" },
        { "compAmount",   "Compressor",    &M::compAmount,    MK::continuous, 0.0f,    1.0f,     0.0f,    "",   "" },
        { "compAttack",   "Comp Attack",   &M::compAttackMs,  MK::continuous, 0.1f,    100.0f,   10.0f,   "ms", "" },
        { "compRelease",  "Comp Release",  &M::compReleaseMs, MK::continuous, 20.0f,   1000.0f,  150.0f,  "ms", "" },
        { "limiterBoost", "Limiter Boost", &M::limiterBoostDb,MK::continuous, 0.0f,    18.0f,    0.0f,    "dB", "" },
        { "bassMono",     "Bass Mono",     &M::bassMonoHz,    MK::continuous, 0.0f,    400.0f,   0.0f,    "Hz", "" },
        { "widthLow",     "Low Width",     &M::widthLow,      MK::continuous, 0.0f,    2.0f,     0.0f,    "",   "" },
        { "widthMid",     "Mid Width",     &M::widthMid,      MK::continuous, 0.0f,    2.0f,     0.0f,    "",   "" },
        { "widthHigh",    "High Width",    &M::widthHigh,     MK::continuous, 0.0f,    2.0f,     0.0f,    "",   "" },
        { "imagerLowHz",  "Low/Mid Split", &M::imagerLowHz,   MK::continuous, 60.0f,   600.0f,   200.0f,  "Hz", "" },
        { "imagerHighHz", "Mid/High Split",&M::imagerHighHz,  MK::continuous, 1000.0f, 10000.0f, 3000.0f, "Hz", "" },
    };

    using SK = SampleField::Kind;
    using S = SampleLayerParams;

    constexpr SampleField sampleFields[] =
    {
        { "on",         "On",          &S::enabled,    SK::toggle,     0.0f,   1.0f,    0.0f,   "",   "" },
        { "mode",       "Mode",        &S::mode,       SK::choice,     0.0f,   3.0f,    0.0f,   "",   "Stretch|Resample|Loop|One-shot" },
        { "level",      "Mix",         &S::levelDb,    SK::continuous, -60.0f, 12.0f,   0.0f,   "dB", "" },
        { "pan",        "Pan",         &S::pan,        SK::continuous, -1.0f,  1.0f,    0.0f,   "",   "" },
        { "width",      "Width",       &S::width,      SK::continuous, 0.0f,   1.0f,    0.0f,   "",   "" },
        { "start",      "Start",       &S::start,      SK::continuous, 0.0f,   1.0f,    0.0f,   "",   "" },
        { "end",        "End",         &S::end,        SK::continuous, 0.0f,   1.0f,    0.0f,   "",   "" },
        { "trimStart",  "Trim Start",  &S::trimStart,  SK::continuous, 0.0f,   1.0f,    0.0f,   "",   "" },
        { "trimEnd",    "Trim End",    &S::trimEnd,    SK::continuous, 0.0f,   1.0f,    0.0f,   "",   "" },
        { "reverse",    "Reverse",     &S::reverse,    SK::toggle,     0.0f,   1.0f,    0.0f,   "",   "" },
        { "fadeIn",     "Fade In",     &S::fadeInMs,   SK::continuous, 0.0f,   2000.0f, 100.0f, "ms", "" },
        { "fadeOut",    "Fade Out",    &S::fadeOutMs,  SK::continuous, 0.0f,   2000.0f, 100.0f, "ms", "" },
        { "pitch",      "Pitch",       &S::pitch,      SK::continuous, -24.0f, 24.0f,   0.0f,   "st", "" },
        { "ampStart",   "Amp Start",   &S::ampStart,   SK::continuous, 0.0f,   1.0f,    0.0f,   "",   "" },
        { "ampEnd",     "Amp End",     &S::ampEnd,     SK::continuous, 0.0f,   1.0f,    0.0f,   "",   "" },
        { "ampShape",   "Amp Curve",   &S::ampShape,   SK::choice,     0.0f,   2.0f,    0.0f,   "",   "Linear|Exponential|S-Curve" },
        { "ampTension", "Amp Tension", &S::ampTension, SK::continuous, -1.0f,  1.0f,    0.0f,   "",   "" },
    };
}

std::span<const SampleField> getSampleLayerFields() noexcept
{
    return sampleFields;
}

float getDefaultValue (const SampleField& field) noexcept
{
    static const SampleLayerParams defaults;
    return defaults.*(field.member);
}

std::span<const MasterField> getMasterFields() noexcept
{
    return masterFields;
}

float getDefaultValue (const MasterField& field) noexcept
{
    static const MasterParams defaults;
    return defaults.*(field.member);
}

std::span<const LayerField> getSynthLayerFields() noexcept
{
    return fields;
}

float getDefaultValue (const LayerField& field) noexcept
{
    static const SynthLayerParams defaults;
    return defaults.*(field.member);
}

} // namespace fxtend
