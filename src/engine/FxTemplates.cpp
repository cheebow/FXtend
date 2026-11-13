#include "FxTemplates.h"

#include <algorithm>
#include <cmath>

namespace fxtend
{

namespace
{
    constexpr float shapeLinear = 0.0f;
    constexpr float shapeExp = 1.0f;
    constexpr float shapeS = 2.0f;
    constexpr float shapeCustom = 3.0f;

    constexpr float filterLow = 1.0f;
    constexpr float filterHigh = 2.0f;
    constexpr float filterBand = 3.0f;

    SynthLayerParams layer (SynthType type, float levelDb)
    {
        SynthLayerParams p;
        p.enabled = 1.0f;
        p.type = (float) (int) type;
        p.levelDb = levelDb;
        return p;
    }

    void setAmp (SynthLayerParams& p, float start, float end, float shape, float tension)
    {
        p.ampStart = start;
        p.ampEnd = end;
        p.ampShape = shape;
        p.ampTension = tension;
    }

    void setPitch (SynthLayerParams& p, float baseHz, float start, float end, float shape, float tension)
    {
        p.baseHz = baseHz;
        p.pitchStart = start;
        p.pitchEnd = end;
        p.pitchShape = shape;
        p.pitchTension = tension;
    }

    void setFilter (SynthLayerParams& p, float type, float start, float end, float shape, float tension, float resonance)
    {
        p.filterType = type;
        p.cutoffStart = start;
        p.cutoffEnd = end;
        p.cutoffShape = shape;
        p.cutoffTension = tension;
        p.resonance = resonance;
    }

    // Macro helpers. m is 0..1 with 0.5 meaning "leave as is".
    float bipolar (float m) noexcept                        { return 2.0f * (std::clamp (m, 0.0f, 1.0f) - 0.5f); }
    float octaves (float m, float range) noexcept           { return std::pow (2.0f, bipolar (m) * range); }
    float clampHz (float hz) noexcept                       { return std::clamp (hz, 20.0f, 20000.0f); }

    /** Moves a 0..1 value towards 0 (m < 0.5) or towards 1 (m > 0.5). */
    float towardsEnds (float value, float m) noexcept
    {
        const float b = bipolar (m);
        return b < 0.0f ? value * (1.0f + b) : value + (1.0f - value) * b;
    }

    bool isType (const SynthLayerParams& p, SynthType type) noexcept { return p.getType() == type; }

    bool isNoiseBased (const SynthLayerParams& p) noexcept
    {
        return ! isType (p, SynthType::sub) && ! isType (p, SynthType::fm);
    }

    void scaleCutoff (SynthLayerParams& p, float factor) noexcept
    {
        p.cutoffStart = clampHz (p.cutoffStart * factor);
        p.cutoffEnd = clampHz (p.cutoffEnd * factor);
    }

    void addTension (float& tension, float amount) noexcept
    {
        tension = std::clamp (tension + amount, -1.0f, 1.0f);
    }
}

const char* getFxTypeName (FxType type) noexcept
{
    switch (type)
    {
        case FxType::impact:        return "Impact";
        case FxType::riser:         return "Riser";
        case FxType::downlifter:    return "Downlifter";
        case FxType::sweep:         return "Sweep";
        case FxType::clap:          return "Clap";
        case FxType::subDrop:       return "Sub Drop";
    }

    return "";
}

const char* getMacroName (FxType type, int macroIndex) noexcept
{
    static const char* const names[numFxTypes][numMacros] =
    {
        { "Punch",       "Sub",          "Brightness",  "Tail"  },
        { "Rise",        "Tension",      "Brightness",  "Width" },
        { "Fall",        "Depth",        "Air",         "Width" },
        { "Direction",   "Cutoff Range", "Noise Color", "Width" },
        { "Density",     "Spread",       "Body",        "Tail"  },
        { "Start Pitch", "End Pitch",    "Weight",      "Drive" },
    };

    const int typeIndex = std::clamp ((int) type, 0, numFxTypes - 1);
    return names[typeIndex][std::clamp (macroIndex, 0, numMacros - 1)];
}

Patch makeTemplate (FxType type)
{
    Patch patch;
    patch.fxType = type;
    patch.tailMode = getDefaultTailMode (type);
    auto& s = patch.synth;

    switch (type)
    {
        case FxType::impact:
        {
            // The boom: a clean sub that drops like a kick drum in the first
            // moments (in real time, so it hits just as hard at any length)
            // and then rings out over the body.
            s[0] = layer (SynthType::sub, 0.0f);
            setPitch (s[0], 46.0f, 2.0f, 0.0f, shapeExp, -0.8f);
            setAmp (s[0], 1.0f, 0.0f, shapeExp, -0.8f);
            s[0].pitchDropSt = 30.0f;
            s[0].pitchDropMs = 45.0f;
            s[0].drive = 0.12f;

            // The click at the very front.
            s[1] = layer (SynthType::transient, 8.0f);
            s[1].decayMs = 4.0f;
            setFilter (s[1], filterHigh, 3000.0f, 3000.0f, shapeLinear, 0.0f, 0.1f);

            // The crash: a wide band of noise that hangs in the air. The mids
            // between it and the sub stay empty, which is what keeps the hit clear.
            s[2] = layer (SynthType::noise, -7.0f);
            setFilter (s[2], filterBand, 2600.0f, 1500.0f, shapeExp, -0.5f, 0.05f);
            setAmp (s[2], 1.0f, 0.0f, shapeExp, -0.75f);
            s[2].width = 1.0f;

            // The sizzle on top, gone sooner.
            s[3] = layer (SynthType::noise, -11.0f);
            setFilter (s[3], filterBand, 6500.0f, 5000.0f, shapeLinear, 0.0f, 0.05f);
            setAmp (s[3], 1.0f, 0.0f, shapeExp, -0.9f);
            s[3].width = 1.0f;

            patch.master.drive = 0.1f;
            patch.master.reverbMix = 0.32f;
            patch.master.reverbSize = 0.85f;
            patch.master.reverbDamping = 0.45f;

            // Squeezed and pushed a little so the body after the hit stays big.
            patch.master.compAmount = 0.25f;
            patch.master.limiterBoostDb = 3.0f;
            break;
        }

        case FxType::riser:
        {
            s[0] = layer (SynthType::noise, -8.0f);
            setFilter (s[0], filterLow, 350.0f, 13000.0f, shapeExp, 0.25f, 0.4f);
            setAmp (s[0], 0.04f, 1.0f, shapeExp, 0.35f);
            s[0].width = 0.75f;

            s[1] = layer (SynthType::fm, -15.0f);
            setPitch (s[1], 110.0f, 0.0f, 24.0f, shapeExp, 0.3f);
            setAmp (s[1], 0.0f, 1.0f, shapeExp, 0.3f);
            s[1].fmRatio = 2.0f;
            s[1].fmIndexStart = 0.8f;
            s[1].fmIndexEnd = 3.5f;
            s[1].width = 0.7f;

            s[2] = layer (SynthType::sub, -14.0f);
            setPitch (s[2], 55.0f, 0.0f, 12.0f, shapeExp, 0.3f);
            setAmp (s[2], 0.0f, 1.0f, shapeLinear, 0.0f);

            s[3] = layer (SynthType::noise, -16.0f);
            s[3].variant = 1.0f;
            setFilter (s[3], filterHigh, 2500.0f, 9000.0f, shapeLinear, 0.0f, 0.2f);
            setAmp (s[3], 0.0f, 1.0f, shapeS, 0.0f);
            s[3].width = 0.9f;
            break;
        }

        case FxType::downlifter:
        {
            s[0] = layer (SynthType::noise, -8.0f);
            setFilter (s[0], filterLow, 13000.0f, 250.0f, shapeExp, -0.3f, 0.4f);
            setAmp (s[0], 1.0f, 0.0f, shapeExp, -0.2f);
            s[0].width = 0.75f;

            s[1] = layer (SynthType::fm, -15.0f);
            setPitch (s[1], 110.0f, 24.0f, 0.0f, shapeExp, -0.3f);
            setAmp (s[1], 1.0f, 0.0f, shapeExp, -0.2f);
            s[1].fmRatio = 2.0f;
            s[1].fmIndexStart = 3.5f;
            s[1].fmIndexEnd = 0.5f;
            s[1].width = 0.7f;

            s[2] = layer (SynthType::sub, -12.0f);
            setPitch (s[2], 55.0f, 12.0f, -12.0f, shapeExp, -0.3f);
            setAmp (s[2], 1.0f, 0.0f, shapeLinear, 0.0f);

            s[3] = layer (SynthType::noise, -16.0f);
            s[3].variant = 1.0f;
            setFilter (s[3], filterHigh, 9000.0f, 2500.0f, shapeLinear, 0.0f, 0.2f);
            setAmp (s[3], 1.0f, 0.0f, shapeExp, -0.5f);
            s[3].width = 0.9f;
            break;
        }

        case FxType::sweep:
        {
            // Builds all the way to the end point, where the filter arrives too.
            s[0] = layer (SynthType::noise, -3.0f);
            s[0].variant = 1.0f;
            setFilter (s[0], filterBand, 300.0f, 9000.0f, shapeS, -0.4f, 0.45f);
            setAmp (s[0], 0.0f, 1.0f, shapeCustom, 0.0f);
            s[0].ampPoints = { 0.0f, 0.18f, 0.5f, 1.0f };
            s[0].width = 0.8f;

            s[1] = layer (SynthType::noise, -9.0f);
            s[1].variant = 1.0f;
            setFilter (s[1], filterLow, 200.0f, 5000.0f, shapeS, -0.4f, 0.2f);
            setAmp (s[1], 0.0f, 1.0f, shapeCustom, 0.0f);
            s[1].ampPoints = { 0.0f, 0.2f, 0.5f, 1.0f };
            s[1].width = 0.6f;
            break;
        }

        case FxType::clap:
        {
            // Every decay here runs in real time: a clap is a clap at any length.
            // Hands: a few bursts, then the last one rings out.
            s[0] = layer (SynthType::clap, 4.0f);
            s[0].count = 3.0f;
            s[0].spreadMs = 9.0f;
            s[0].decayMs = 8.0f;
            s[0].ampDecayMs = 260.0f;
            s[0].drive = 0.2f;
            setFilter (s[0], filterBand, 1500.0f, 1500.0f, shapeLinear, 0.0f, 0.12f);
            s[0].width = 0.5f;

            // Crack.
            s[1] = layer (SynthType::transient, -2.0f);
            s[1].decayMs = 12.0f;
            setFilter (s[1], filterBand, 2600.0f, 2600.0f, shapeLinear, 0.0f, 0.1f);

            // Upper part of the ring.
            s[2] = layer (SynthType::noise, -12.0f);
            s[2].ampDecayMs = 200.0f;
            setFilter (s[2], filterBand, 3200.0f, 3200.0f, shapeLinear, 0.0f, 0.1f);
            s[2].width = 0.8f;

            // Body: the low thump of the palms.
            s[3] = layer (SynthType::transient, -6.0f);
            s[3].decayMs = 40.0f;
            setFilter (s[3], filterBand, 600.0f, 600.0f, shapeLinear, 0.0f, 0.2f);

            patch.master.lowPassHz = 9000.0f;
            patch.master.drive = 0.15f;
            patch.master.reverbMix = 0.38f;
            patch.master.reverbSize = 0.7f;
            patch.master.reverbDamping = 0.6f;
            break;
        }

        case FxType::subDrop:
        {
            s[0] = layer (SynthType::sub, -3.0f);
            setPitch (s[0], 36.0f, 36.0f, 0.0f, shapeExp, -0.45f);
            setAmp (s[0], 1.0f, 0.75f, shapeLinear, 0.0f);
            s[0].attackMs = 4.0f;
            s[0].drive = 0.15f;

            s[1] = layer (SynthType::sub, -16.0f);
            s[1].variant = 1.0f;
            setPitch (s[1], 72.0f, 36.0f, 0.0f, shapeExp, -0.45f);
            setAmp (s[1], 1.0f, 0.3f, shapeExp, -0.3f);
            s[1].attackMs = 4.0f;
            setFilter (s[1], filterLow, 1200.0f, 300.0f, shapeLinear, 0.0f, 0.1f);
            break;
        }
    }

    return patch;
}

Patch applyMacros (const Patch& source) noexcept
{
    Patch patch = source;
    const auto& m = source.macros;

    for (auto& p : patch.synth)
    {
        if (! p.isEnabled())
            continue;

        switch (source.fxType)
        {
            case FxType::impact:
            {
                // Punch
                if (isType (p, SynthType::transient))
                    p.levelDb += bipolar (m[0]) * 9.0f;

                if (isType (p, SynthType::sub))
                {
                    p.pitchStart *= 2.0f * m[0];
                    p.pitchDropSt = std::clamp (p.pitchDropSt * 2.0f * m[0], 0.0f, 60.0f);
                }

                if (isType (p, SynthType::fm))
                    p.fmIndexStart *= 2.0f * m[0];

                // Sub
                if (isType (p, SynthType::sub))
                    p.levelDb += bipolar (m[1]) * 12.0f;

                // Brightness
                scaleCutoff (p, octaves (m[2], 2.0f));

                if (isType (p, SynthType::fm))
                    p.levelDb += bipolar (m[2]) * 6.0f;

                // Tail: how long the decay hangs on before the end point.
                addTension (p.ampTension, bipolar (m[3]) * 0.6f);
                break;
            }

            case FxType::riser:
            {
                // Rise: how far pitch and filter travel.
                p.pitchEnd *= 2.0f * m[0];
                p.cutoffEnd = clampHz (p.cutoffEnd * octaves (m[0], 1.5f));

                // Tension
                p.resonance = std::clamp (p.resonance + bipolar (m[1]) * 0.35f, 0.0f, 1.0f);
                p.fmIndexEnd *= 2.0f * m[1];
                addTension (p.ampTension, bipolar (m[1]) * 0.5f);

                // Brightness
                scaleCutoff (p, octaves (m[2], 1.5f));

                // Width
                p.width = towardsEnds (p.width, m[3]);
                break;
            }

            case FxType::downlifter:
            {
                // Fall: how far pitch and filter travel.
                p.pitchStart *= 2.0f * m[0];
                p.cutoffStart = clampHz (p.cutoffStart * octaves (m[0], 1.5f));

                // Depth: how low it lands.
                if (isType (p, SynthType::sub))
                {
                    p.pitchEnd += bipolar (m[1]) * -12.0f;
                    p.levelDb += bipolar (m[1]) * 9.0f;
                }

                p.cutoffEnd = clampHz (p.cutoffEnd * octaves (m[1], -1.0f));

                // Air: the high-passed layer.
                if (isType (p, SynthType::noise) && p.getFilterType() == FilterType::highPass)
                    p.levelDb += bipolar (m[2]) * 12.0f;

                // Width
                p.width = towardsEnds (p.width, m[3]);
                break;
            }

            case FxType::sweep:
            {
                // Cutoff Range: widen or narrow around the centre of the sweep.
                const float centre = std::sqrt (p.cutoffStart * p.cutoffEnd);
                const float spread = octaves (m[1], 1.0f);
                p.cutoffStart = clampHz (centre * std::pow (p.cutoffStart / centre, spread));
                p.cutoffEnd = clampHz (centre * std::pow (p.cutoffEnd / centre, spread));

                // Direction: below the middle the sweep runs downwards.
                if (m[0] < 0.5f)
                    std::swap (p.cutoffStart, p.cutoffEnd);

                // Noise Color: brown - pink - white.
                if (isType (p, SynthType::noise))
                    p.variant = m[2] < 1.0f / 3.0f ? 2.0f : (m[2] < 2.0f / 3.0f ? 1.0f : 0.0f);

                // Width
                p.width = towardsEnds (p.width, m[3]);
                break;
            }

            case FxType::clap:
            {
                if (isType (p, SynthType::clap))
                {
                    // Density / Spread
                    p.count = std::clamp (p.count + std::round (bipolar (m[0]) * 2.0f), 2.0f, 8.0f);
                    p.spreadMs = std::clamp (p.spreadMs * octaves (m[1], 1.5f), 1.0f, 200.0f);
                }

                // Body: lower and fuller, or thinner and brighter.
                if (isNoiseBased (p))
                    scaleCutoff (p, octaves (m[2], -0.75f));

                if (isType (p, SynthType::noise))
                    p.levelDb += bipolar (m[2]) * 9.0f;

                // Tail
                addTension (p.ampTension, bipolar (m[3]) * 0.9f);
                p.ampDecayMs = std::min (5000.0f, p.ampDecayMs * octaves (m[3], 1.5f));
                break;
            }

            case FxType::subDrop:
            {
                if (isType (p, SynthType::sub))
                {
                    p.pitchStart += bipolar (m[0]) * 24.0f;
                    p.pitchEnd += bipolar (m[1]) * 12.0f;
                    p.drive = std::clamp (p.drive + bipolar (m[3]) * 0.6f, 0.0f, 1.0f);

                    // Weight: balance between the fundamental and the upper layer.
                    p.levelDb += bipolar (m[2]) * (p.getVariant() == 0 ? 6.0f : -9.0f);
                }

                break;
            }
        }
    }

    // The Tail macro of Impact and Clap also opens up the reverb.
    if (source.fxType == FxType::impact || source.fxType == FxType::clap)
        patch.master.reverbMix = std::clamp (patch.master.reverbMix * 2.0f * m[3], 0.0f, 1.0f);

    return patch;
}

} // namespace fxtend
