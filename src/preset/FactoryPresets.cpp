#include "FactoryPresets.h"
#include "../engine/FxTemplates.h"

namespace fxtend
{

namespace
{
    using Tweak = void (*) (Patch&);

    PresetData make (const char* name, FxType type, Tweak tweak = nullptr)
    {
        PresetData preset;
        preset.name = name;
        preset.patch = makeTemplate (type);

        if (tweak != nullptr)
            tweak (preset.patch);

        return preset;
    }

    constexpr float noise = 0.0f, pink = 1.0f, brown = 2.0f;
    constexpr float lowPass = 1.0f, highPass = 2.0f, bandPass = 3.0f;
    constexpr float linear = 0.0f, exponential = 1.0f, custom = 3.0f;

    /** Fades a layer out over the body (rather than in real time). */
    void setFade (SynthLayerParams& layer, float tension)
    {
        layer.ampStart = 1.0f;
        layer.ampEnd = 0.0f;
        layer.ampShape = exponential;
        layer.ampTension = tension;
    }

    /** Turns a noise layer into the long ring of a clap: a broad band that fades over the body. */
    void makeRing (SynthLayerParams& layer, float centreHz, float levelDb, float tension)
    {
        layer.ampDecayMs = 0.0f;
        layer.levelDb = levelDb;
        layer.filterType = bandPass;
        layer.cutoffStart = layer.cutoffEnd = centreHz;
        layer.resonance = 0.05f;
        setFade (layer, tension);
    }

    std::vector<PresetData> build()
    {
        std::vector<PresetData> presets;
        juce::ignoreUnused (linear);

        // ---- Impact (8) ---------------------------------------------------
        // Layers: 0 sub boom, 1 click, 2 crash noise, 3 sizzle.
        presets.push_back (make ("Impact 01 Deep", FxType::impact));

        presets.push_back (make ("Impact 02 Tight", FxType::impact, [] (Patch& p)
        {
            p.synth[0].ampDecayMs = 380.0f;
            p.synth[2].ampDecayMs = 260.0f;
            p.synth[3].ampDecayMs = 160.0f;
            p.master.reverbMix = 0.15f;
            p.tailMode = TailMode::strict;
        }));

        presets.push_back (make ("Impact 03 Metallic", FxType::impact, [] (Patch& p)
        {
            p.synth[3] = p.synth[2];
            p.synth[3].levelDb = -13.0f;

            auto& metal = p.synth[2];
            metal = SynthLayerParams();
            metal.enabled = 1.0f;
            metal.type = 2.0f;      // FM
            metal.levelDb = -10.0f;
            metal.baseHz = 220.0f;
            metal.pitchEnd = -2.0f;
            metal.fmRatio = 4.73f;
            metal.fmIndexStart = 8.0f;
            metal.fmIndexEnd = 0.5f;
            metal.cutoffShape = exponential;
            metal.cutoffTension = -0.7f;
            metal.width = 0.9f;
            setFade (metal, -0.8f);

            p.synth[0].levelDb = -4.0f;
        }));

        presets.push_back (make ("Impact 04 Boom", FxType::impact, [] (Patch& p)
        {
            p.synth[0].baseHz = 40.0f;
            p.synth[0].pitchDropSt = 36.0f;
            p.synth[0].pitchDropMs = 70.0f;
            p.synth[0].levelDb = 3.0f;
            p.synth[0].drive = 0.3f;
            p.synth[1].levelDb = -3.0f;
            p.synth[2].levelDb = -19.0f;
            p.synth[3].enabled = 0.0f;
            p.macros = { 0.5f, 0.5f, 0.4f, 0.6f };
        }));

        presets.push_back (make ("Impact 05 Bright Crash", FxType::impact, [] (Patch& p)
        {
            // The sub is only a short thump; the crash carries it.
            p.synth[0].ampDecayMs = 140.0f;
            p.synth[0].levelDb = -2.0f;
            p.synth[2].levelDb = -7.0f;
            p.synth[3].levelDb = -8.0f;
            setFade (p.synth[3], -0.8f);
            p.macros = { 0.55f, 0.5f, 0.6f, 0.55f };
        }));

        presets.push_back (make ("Impact 06 Distorted", FxType::impact, [] (Patch& p)
        {
            p.synth[0].drive = 0.85f;
            p.synth[0].levelDb = -3.0f;
            p.synth[2].drive = 0.5f;
            p.synth[2].levelDb = -15.0f;
            p.master.drive = 0.55f;
        }));

        presets.push_back (make ("Impact 07 Double Hit", FxType::impact, [] (Patch& p)
        {
            p.synth[1].count = 2.0f;
            p.synth[1].spreadMs = 110.0f;
            p.synth[1].decayMs = 30.0f;
            p.synth[1].cutoffStart = p.synth[1].cutoffEnd = 600.0f;
            p.synth[1].levelDb = 4.0f;
        }));

        presets.push_back (make ("Impact 08 Long Rumble", FxType::impact, [] (Patch& p)
        {
            p.synth[0].baseHz = 55.0f;
            p.synth[0].pitchDropSt = 14.0f;
            p.synth[0].pitchDropMs = 90.0f;
            p.synth[0].ampTension = -0.5f;
            p.synth[1].levelDb = -8.0f;
            p.synth[2].levelDb = -10.0f;
            p.synth[2].ampTension = -0.5f;
            p.synth[3].levelDb = -13.0f;
            p.synth[3].ampTension = -0.6f;
            p.macros = { 0.5f, 0.5f, 0.5f, 0.6f };
        }));

        // A hall the size of a cathedral around a lower, darker hit.
        presets.push_back (make ("Impact 09 Cinematic", FxType::impact, [] (Patch& p)
        {
            p.synth[0].baseHz = 38.0f;
            p.synth[2].cutoffStart = 1400.0f;
            p.synth[2].cutoffEnd = 700.0f;
            p.synth[2].levelDb = -5.0f;
            p.master.reverbType = 3.0f;     // Cathedral
            p.master.reverbMix = 0.45f;
            p.master.reverbSize = 0.9f;
            p.macros = { 0.5f, 0.6f, 0.4f, 0.6f };
        }));

        // The sizzle swapped for a falling FM zap.
        presets.push_back (make ("Impact 10 Laser", FxType::impact, [] (Patch& p)
        {
            auto& zap = p.synth[3];
            zap = SynthLayerParams();
            zap.enabled = 1.0f;
            zap.type = 2.0f;                // FM
            zap.levelDb = -9.0f;
            zap.baseHz = 220.0f;
            zap.pitchStart = 36.0f;
            zap.pitchEnd = 0.0f;
            zap.pitchShape = exponential;
            zap.pitchTension = -0.8f;
            zap.fmRatio = 3.5f;
            zap.fmIndexStart = 4.0f;
            zap.fmIndexEnd = 0.5f;
            zap.width = 0.8f;
            setFade (zap, -0.85f);
            p.master.delayMix = 0.15f;
        }));

        // Hardly any room, more squeeze: all front.
        presets.push_back (make ("Impact 11 Dry Punch", FxType::impact, [] (Patch& p)
        {
            p.synth[0].pitchDropSt = 36.0f;
            p.synth[1].levelDb = 10.0f;
            p.synth[2].levelDb = -12.0f;
            p.synth[2].ampTension = -0.9f;
            p.synth[3].enabled = 0.0f;
            p.master.reverbType = 1.0f;     // Room
            p.master.reverbMix = 0.08f;
            p.master.compAmount = 0.45f;
            p.macros = { 0.7f, 0.5f, 0.5f, 0.3f };
        }));

        // The hit comes back as worn tape echoes.
        presets.push_back (make ("Impact 12 Echo Slam", FxType::impact, [] (Patch& p)
        {
            p.master.delayType = 2.0f;      // Tape
            p.master.delayMix = 0.32f;
            p.master.delayTime = 3.0f;      // 1/4
            p.master.delayFeedback = 0.5f;
            p.master.reverbMix = 0.22f;
        }));

        // ---- Riser (6) ----------------------------------------------------
        presets.push_back (make ("Riser 01 Classic Noise", FxType::riser));

        presets.push_back (make ("Riser 02 Tonal", FxType::riser, [] (Patch& p)
        {
            p.synth[1].levelDb = -8.0f;
            p.synth[1].fmIndexEnd = 2.0f;
            p.synth[0].levelDb = -15.0f;
            p.synth[2].levelDb = -10.0f;
        }));

        presets.push_back (make ("Riser 03 Tension", FxType::riser, [] (Patch& p)
        {
            p.synth[0].filterType = bandPass;
            p.synth[0].levelDb = -3.0f;
            p.macros = { 0.7f, 0.85f, 0.5f, 0.6f };
        }));

        presets.push_back (make ("Riser 04 Airy", FxType::riser, [] (Patch& p)
        {
            p.synth[0].variant = pink;
            p.synth[0].filterType = highPass;
            p.synth[0].cutoffStart = 600.0f;
            p.synth[0].cutoffEnd = 7000.0f;
            p.synth[0].levelDb = -5.0f;
            p.synth[1].enabled = 0.0f;
            p.synth[2].enabled = 0.0f;
            p.synth[3].levelDb = -9.0f;
            p.macros = { 0.5f, 0.35f, 0.6f, 0.8f };
        }));

        presets.push_back (make ("Riser 05 Sub Swell", FxType::riser, [] (Patch& p)
        {
            p.synth[2].levelDb = -5.0f;
            p.synth[2].pitchEnd = 24.0f;
            p.synth[2].baseHz = 41.0f;
            p.synth[0].levelDb = -16.0f;
            p.synth[1].levelDb = -20.0f;
            p.synth[3].enabled = 0.0f;
        }));

        presets.push_back (make ("Riser 06 Late Bloom", FxType::riser, [] (Patch& p)
        {
            for (auto& layer : p.synth)
            {
                layer.ampShape = exponential;
                layer.ampTension = 0.8f;
            }

            p.synth[0].cutoffTension = 0.7f;
            p.synth[1].pitchTension = 0.8f;
            p.macros = { 0.65f, 0.6f, 0.55f, 0.5f };
        }));

        // A tone that wails faster and faster as it climbs.
        presets.push_back (make ("Riser 07 Siren", FxType::riser, [] (Patch& p)
        {
            p.synth[1].levelDb = -7.0f;
            p.synth[1].fmIndexEnd = 1.5f;
            p.synth[1].lfoPitchSt = 3.0f;
            p.synth[1].lfoRateStart = 2.0f;
            p.synth[1].lfoRateEnd = 12.0f;
            p.synth[0].levelDb = -14.0f;
        }));

        // Chopped by a gate that speeds up towards the end.
        presets.push_back (make ("Riser 08 Stutter Gate", FxType::riser, [] (Patch& p)
        {
            for (int i : { 0, 3 })
            {
                p.synth[(size_t) i].lfoShape = 2.0f;        // square
                p.synth[(size_t) i].lfoAmp = 1.0f;
                p.synth[(size_t) i].lfoRateStart = 4.0f;
                p.synth[(size_t) i].lfoRateEnd = 24.0f;
            }

            p.synth[0].levelDb = -5.0f;
            p.synth[1].levelDb = -18.0f;
        }));

        presets.push_back (make ("Riser 09 Wide Wash", FxType::riser, [] (Patch& p)
        {
            p.synth[0].variant = pink;
            p.synth[0].filterType = bandPass;
            p.synth[0].resonance = 0.3f;
            p.synth[0].levelDb = -4.0f;
            p.synth[0].width = 1.0f;
            p.synth[1].enabled = 0.0f;
            p.master.reverbMix = 0.35f;
            p.master.reverbSize = 0.8f;
            p.master.widthHigh = 1.6f;
            p.macros = { 0.5f, 0.5f, 0.55f, 0.8f };
        }));

        // Three octaves of bright FM.
        presets.push_back (make ("Riser 10 Laser Climb", FxType::riser, [] (Patch& p)
        {
            p.synth[1].levelDb = -9.0f;
            p.synth[1].pitchEnd = 36.0f;
            p.synth[1].fmRatio = 1.0f;
            p.synth[1].fmIndexEnd = 6.0f;
            p.synth[0].levelDb = -16.0f;
            p.synth[2].enabled = 0.0f;
        }));

        presets.push_back (make ("Riser 11 Tape Echo", FxType::riser, [] (Patch& p)
        {
            p.synth[1].levelDb = -11.0f;
            p.master.delayType = 2.0f;      // Tape
            p.master.delayMix = 0.3f;
            p.master.delayFeedback = 0.55f;
            p.master.reverbMix = 0.15f;
        }));

        // ---- Downlifter (6) -----------------------------------------------
        presets.push_back (make ("Downlifter 01 Classic", FxType::downlifter));

        presets.push_back (make ("Downlifter 02 Tonal Fall", FxType::downlifter, [] (Patch& p)
        {
            p.synth[1].levelDb = -8.0f;
            p.synth[0].levelDb = -15.0f;
            p.synth[2].levelDb = -9.0f;
        }));

        presets.push_back (make ("Downlifter 03 Deep Dive", FxType::downlifter, [] (Patch& p)
        {
            p.synth[2].levelDb = -6.0f;
            p.synth[2].pitchStart = 24.0f;
            p.macros = { 0.6f, 0.85f, 0.4f, 0.5f };
        }));

        presets.push_back (make ("Downlifter 04 Air Release", FxType::downlifter, [] (Patch& p)
        {
            p.synth[1].enabled = 0.0f;
            p.synth[2].enabled = 0.0f;
            p.synth[0].variant = pink;
            p.synth[0].levelDb = -5.0f;
            p.macros = { 0.5f, 0.4f, 0.8f, 0.85f };
        }));

        presets.push_back (make ("Downlifter 05 Fast Drop", FxType::downlifter, [] (Patch& p)
        {
            for (auto& layer : p.synth)
                layer.ampTension = -0.7f;

            p.synth[0].cutoffTension = -0.8f;
            p.synth[1].pitchTension = -0.8f;
            p.synth[2].pitchTension = -0.8f;
        }));

        presets.push_back (make ("Downlifter 06 Resonant", FxType::downlifter, [] (Patch& p)
        {
            p.synth[0].filterType = bandPass;
            p.synth[0].resonance = 0.7f;
            p.synth[0].levelDb = -2.0f;
            p.synth[1].levelDb = -18.0f;
        }));

        // ---- Falls with an LFO -------------------------------------------
        // A whistle that wobbles on its way down, the wobble slowing as it falls.
        presets.push_back (make ("Fall 01 Whistle", FxType::downlifter, [] (Patch& p)
        {
            for (auto& layer : p.synth)
                layer = SynthLayerParams();

            auto& tone = p.synth[0];
            tone.enabled = 1.0f;
            tone.type = 1.0f;       // Sub (sine)
            tone.levelDb = -6.0f;
            tone.baseHz = 330.0f;
            tone.pitchStart = 30.0f;
            tone.pitchEnd = 0.0f;
            tone.pitchShape = exponential;
            tone.pitchTension = -0.25f;
            tone.attackMs = 15.0f;
            tone.lfoPitchSt = 2.5f;
            tone.lfoRateStart = 9.0f;
            tone.lfoRateEnd = 4.0f;
            setFade (tone, 0.35f);

            // Breath around the tone; noise follows the layer's pitch.
            auto& breath = p.synth[1];
            breath = tone;
            breath.type = 0.0f;     // Noise
            breath.levelDb = -8.0f;
            breath.filterType = bandPass;
            breath.cutoffStart = breath.cutoffEnd = 330.0f;
            breath.resonance = 0.75f;
            breath.width = 0.8f;

            p.master.delayMix = 0.18f;
            p.master.delayFeedback = 0.35f;
            p.master.reverbMix = 0.2f;
            p.macros = { 0.5f, 0.5f, 0.5f, 0.5f };
        }));

        // A fast trill between two notes, tumbling down.
        presets.push_back (make ("Fall 02 Trill", FxType::downlifter, [] (Patch& p)
        {
            for (auto& layer : p.synth)
                layer = SynthLayerParams();

            auto& tone = p.synth[0];
            tone.enabled = 1.0f;
            tone.type = 1.0f;       // Sub
            tone.variant = 1.0f;    // triangle
            tone.levelDb = -7.0f;
            tone.baseHz = 262.0f;
            tone.pitchStart = 31.0f;
            tone.pitchEnd = 0.0f;
            tone.pitchShape = exponential;
            tone.pitchTension = -0.2f;
            tone.lfoShape = 2.0f;   // square
            tone.lfoPitchSt = 3.5f;
            tone.lfoRateStart = 16.0f;
            tone.lfoRateEnd = 9.0f;
            setFade (tone, 0.3f);

            auto& shimmer = p.synth[1];
            shimmer = tone;
            shimmer.type = 2.0f;    // FM
            shimmer.levelDb = -16.0f;
            shimmer.fmRatio = 2.0f;
            shimmer.fmIndexStart = 1.5f;
            shimmer.fmIndexEnd = 0.3f;
            shimmer.width = 1.0f;

            p.master.delayMix = 0.22f;
            p.master.delayTime = 1.0f;
            p.master.reverbMix = 0.18f;
            p.macros = { 0.5f, 0.5f, 0.5f, 0.5f };
        }));

        // Random notes falling: a computer coming down the stairs.
        presets.push_back (make ("Fall 03 Bleeps", FxType::downlifter, [] (Patch& p)
        {
            for (auto& layer : p.synth)
                layer = SynthLayerParams();

            auto& tone = p.synth[0];
            tone.enabled = 1.0f;
            tone.type = 1.0f;       // Sub
            tone.variant = 1.0f;
            tone.levelDb = -7.0f;
            tone.baseHz = 330.0f;
            tone.pitchStart = 26.0f;
            tone.pitchEnd = 2.0f;
            tone.lfoShape = 4.0f;   // random
            tone.lfoPitchSt = 7.0f;
            tone.lfoRateStart = 18.0f;
            tone.lfoRateEnd = 10.0f;
            setFade (tone, 0.3f);

            p.master.delayMix = 0.25f;
            p.master.reverbMix = 0.2f;
            p.macros = { 0.5f, 0.5f, 0.5f, 0.5f };
        }));

        // The classic noise fall with a tremolo that slows down.
        presets.push_back (make ("Fall 04 Flutter Noise", FxType::downlifter, [] (Patch& p)
        {
            p.synth[0].filterType = bandPass;
            p.synth[0].resonance = 0.55f;
            p.synth[0].levelDb = -3.0f;
            p.synth[0].lfoCutoffOct = 0.7f;
            p.synth[0].lfoAmp = 0.7f;
            p.synth[0].lfoRateStart = 14.0f;
            p.synth[0].lfoRateEnd = 3.0f;
            p.synth[1].enabled = 0.0f;
        }));

        // A square-wave jump between two notes on the way down, like an old game.
        presets.push_back (make ("Fall 05 Arcade", FxType::downlifter, [] (Patch& p)
        {
            for (auto& layer : p.synth)
                layer = SynthLayerParams();

            auto& tone = p.synth[0];
            tone.enabled = 1.0f;
            tone.type = 1.0f;       // Sub
            tone.variant = 1.0f;    // triangle
            tone.levelDb = -1.0f;
            tone.baseHz = 440.0f;
            tone.pitchStart = 24.0f;
            tone.pitchEnd = 0.0f;
            tone.lfoShape = 2.0f;   // square
            tone.lfoPitchSt = 7.0f;
            tone.lfoRateStart = 16.0f;
            tone.lfoRateEnd = 8.0f;
            setFade (tone, 0.3f);

            p.master.delayMix = 0.15f;
            p.macros = { 0.5f, 0.5f, 0.5f, 0.5f };
        }));

        // A siren that winds down as it falls.
        presets.push_back (make ("Fall 06 Siren Drop", FxType::downlifter, [] (Patch& p)
        {
            for (auto& layer : p.synth)
                layer = SynthLayerParams();

            auto& tone = p.synth[0];
            tone.enabled = 1.0f;
            tone.type = 2.0f;       // FM
            tone.levelDb = -8.0f;
            tone.baseHz = 220.0f;
            tone.pitchStart = 24.0f;
            tone.pitchEnd = -12.0f;
            tone.fmRatio = 1.0f;
            tone.fmIndexStart = 2.0f;
            tone.fmIndexEnd = 0.5f;
            tone.lfoPitchSt = 4.0f;
            tone.lfoRateStart = 6.0f;
            tone.lfoRateEnd = 1.5f;
            tone.attackMs = 10.0f;
            setFade (tone, 0.2f);

            p.master.reverbMix = 0.25f;
            p.macros = { 0.5f, 0.5f, 0.5f, 0.5f };
        }));

        presets.push_back (make ("Downlifter 07 Wobble", FxType::downlifter, [] (Patch& p)
        {
            p.synth[2].levelDb = -7.0f;
            p.synth[2].lfoPitchSt = 1.5f;
            p.synth[2].lfoRateStart = 8.0f;
            p.synth[2].lfoRateEnd = 2.0f;
            p.synth[0].lfoCutoffOct = 1.0f;
            p.synth[0].lfoRateStart = 8.0f;
            p.synth[0].lfoRateEnd = 2.0f;
        }));

        presets.push_back (make ("Downlifter 08 Dark Plunge", FxType::downlifter, [] (Patch& p)
        {
            p.synth[2].levelDb = -5.0f;
            p.synth[2].pitchEnd = -24.0f;
            p.synth[3].enabled = 0.0f;
            p.master.lowPassHz = 5000.0f;
            p.master.reverbType = 4.0f;     // Dark
            p.master.reverbMix = 0.35f;
            p.macros = { 0.5f, 0.7f, 0.3f, 0.5f };
        }));

        // ---- Sweep / Whoosh (4) -------------------------------------------
        presets.push_back (make ("Sweep 01 Up", FxType::sweep));

        presets.push_back (make ("Sweep 02 Down", FxType::sweep, [] (Patch& p)
        {
            p.macros = { 0.0f, 0.5f, 0.5f, 0.5f };
        }));

        presets.push_back (make ("Whoosh 01 Dark", FxType::sweep, [] (Patch& p)
        {
            p.synth[0].filterType = lowPass;
            p.synth[0].cutoffStart = 150.0f;
            p.synth[0].cutoffEnd = 3000.0f;
            p.synth[0].levelDb = -2.0f;
            p.synth[0].ampPoints = { 0.0f, 0.55f, 1.0f, 0.0f };     // passes by: peaks inside the body
            p.synth[1].ampPoints = { 0.0f, 0.6f, 0.9f, 0.0f };
            p.macros = { 1.0f, 0.5f, 0.1f, 0.6f };
        }));

        presets.push_back (make ("Whoosh 02 Wide Resonant", FxType::sweep, [] (Patch& p)
        {
            p.synth[0].resonance = 0.75f;
            p.synth[0].levelDb = -3.0f;
            p.synth[0].ampPoints = { 0.0f, 0.3f, 1.0f, 0.0f };
            p.synth[1].ampPoints = { 0.0f, 0.6f, 0.9f, 0.0f };
            p.macros = { 1.0f, 0.75f, 0.9f, 1.0f };
        }));

        // Whistling resonance that passes by.
        presets.push_back (make ("Whoosh 03 Jet Flyby", FxType::sweep, [] (Patch& p)
        {
            p.synth[0].variant = noise;
            p.synth[0].resonance = 0.75f;
            p.synth[0].levelDb = -6.0f;
            p.synth[0].cutoffStart = 400.0f;
            p.synth[0].cutoffEnd = 6000.0f;
            p.synth[0].ampPoints = { 0.0f, 1.0f, 0.6f, 0.0f };
            p.synth[1].ampPoints = { 0.0f, 0.9f, 0.6f, 0.0f };
            p.macros = { 1.0f, 0.5f, 0.5f, 0.7f };
        }));

        presets.push_back (make ("Sweep 03 Flutter", FxType::sweep, [] (Patch& p)
        {
            p.synth[0].lfoCutoffOct = 1.2f;
            p.synth[0].lfoRateStart = 3.0f;
            p.synth[0].lfoRateEnd = 9.0f;
        }));

        presets.push_back (make ("Sweep 04 Dark Wind", FxType::sweep, [] (Patch& p)
        {
            p.synth[0].variant = brown;
            p.synth[0].cutoffStart = 200.0f;
            p.synth[0].cutoffEnd = 3000.0f;
            p.synth[0].levelDb = 0.0f;
            p.synth[1].variant = brown;
            p.synth[1].cutoffStart = 150.0f;
            p.synth[1].cutoffEnd = 2500.0f;
            p.synth[1].levelDb = -4.0f;
            p.master.reverbType = 4.0f;     // Dark
            p.master.reverbMix = 0.3f;
        }));

        presets.push_back (make ("Sweep 05 Shimmer", FxType::sweep, [] (Patch& p)
        {
            p.synth[1].filterType = highPass;
            p.synth[1].cutoffStart = 3000.0f;
            p.synth[1].cutoffEnd = 12000.0f;
            p.synth[1].levelDb = -8.0f;
            p.synth[1].width = 1.0f;
            p.master.reverbType = 2.0f;     // Plate
            p.master.reverbMix = 0.3f;
            p.master.widthHigh = 1.5f;
        }));

        presets.push_back (make ("Sweep 06 Tremolo", FxType::sweep, [] (Patch& p)
        {
            for (int i : { 0, 1 })
            {
                p.synth[(size_t) i].lfoShape = 1.0f;        // triangle
                p.synth[(size_t) i].lfoAmp = 0.9f;
                p.synth[(size_t) i].lfoRateStart = 6.0f;
                p.synth[(size_t) i].lfoRateEnd = 16.0f;
            }
        }));

        // ---- Clap / Long Clap (4) -----------------------------------------
        presets.push_back (make ("Clap 01 Tight", FxType::clap, [] (Patch& p)
        {
            p.synth[0].ampDecayMs = 90.0f;
            p.synth[2].ampDecayMs = 70.0f;
            p.master.reverbMix = 0.12f;
            p.tailMode = TailMode::strict;
        }));

        presets.push_back (make ("Clap 02 Roomy", FxType::clap, [] (Patch& p)
        {
            p.synth[0].width = 0.9f;
            p.synth[0].ampDecayMs = 320.0f;
            p.master.reverbMix = 0.5f;
            p.master.reverbSize = 0.8f;
            p.macros = { 0.75f, 0.6f, 0.5f, 0.5f };
        }));

        // Long claps follow the length instead: the hit stays short, and a
        // ring behind it fades out smoothly over the whole body.
        presets.push_back (make ("Long Clap 01 Classic", FxType::clap, [] (Patch& p)
        {
            p.synth[0].ampDecayMs = 110.0f;
            makeRing (p.synth[2], 1300.0f, -5.0f, -0.5f);
            p.master.lowPassHz = 6000.0f;
            p.master.reverbMix = 0.4f;
            p.master.reverbSize = 0.85f;
        }));

        // Bright and airy: many hands flamming across the stereo field, and a
        // hiss of a ring that slides down from the top as it fades.
        presets.push_back (make ("Long Clap 02 Wide Wash", FxType::clap, [] (Patch& p)
        {
            p.synth[0].count = 6.0f;
            p.synth[0].spreadMs = 17.0f;
            p.synth[0].width = 1.0f;
            p.synth[0].ampDecayMs = 160.0f;
            p.synth[0].cutoffStart = p.synth[0].cutoffEnd = 2400.0f;
            makeRing (p.synth[2], 5000.0f, -1.0f, -0.25f);
            p.synth[2].cutoffStart = 7500.0f;
            p.synth[2].cutoffEnd = 2200.0f;
            p.synth[2].width = 1.0f;
            p.synth[2].lfoRateStart = p.synth[2].lfoRateEnd = 0.6f;
            p.synth[2].lfoCutoffOct = 0.35f;
            p.synth[3].levelDb = -14.0f;        // little palm thump: it floats
            p.master.highPassHz = 280.0f;
            p.master.lowPassHz = 15000.0f;
            p.master.tiltDb = 2.0f;
            p.master.reverbMix = 0.55f;
            p.master.reverbSize = 0.95f;
            p.master.reverbDamping = 0.25f;
            p.macros = { 0.5f, 0.7f, 0.5f, 0.5f };
        }));

        // A whole row of hands in a big hall.
        presets.push_back (make ("Clap 03 Stadium", FxType::clap, [] (Patch& p)
        {
            p.synth[0].count = 6.0f;
            p.synth[0].spreadMs = 16.0f;
            p.synth[0].width = 1.0f;
            p.master.reverbMix = 0.5f;
            p.master.reverbSize = 0.85f;
        }));

        presets.push_back (make ("Clap 04 Tape Slap", FxType::clap, [] (Patch& p)
        {
            p.master.delayType = 2.0f;      // Tape
            p.master.delayMix = 0.28f;
            p.master.delayTime = 0.0f;      // 1/16
            p.master.delayFeedback = 0.3f;
            p.master.reverbMix = 0.15f;
        }));

        // Two quick bursts, bright and short: closer to a finger snap.
        presets.push_back (make ("Clap 05 Snap", FxType::clap, [] (Patch& p)
        {
            p.synth[0].count = 2.0f;
            p.synth[0].spreadMs = 5.0f;
            p.synth[0].decayMs = 5.0f;
            p.synth[0].ampDecayMs = 120.0f;
            p.synth[0].cutoffStart = p.synth[0].cutoffEnd = 2400.0f;
            p.synth[1].cutoffStart = p.synth[1].cutoffEnd = 3800.0f;
            p.synth[3].enabled = 0.0f;
            p.master.reverbType = 1.0f;     // Room
            p.master.reverbMix = 0.2f;
        }));

        presets.push_back (make ("Clap 06 Lo-Fi", FxType::clap, [] (Patch& p)
        {
            p.master.highPassHz = 250.0f;
            p.master.lowPassHz = 4500.0f;
            p.master.drive = 0.5f;
            p.master.reverbMix = 0.2f;
        }));

        // Dark and slow: a dry, low hit, then the ring is almost gone before
        // it swells back in a third of the way through and echoes away.
        presets.push_back (make ("Long Clap 03 Plate Bloom", FxType::clap, [] (Patch& p)
        {
            p.synth[0].ampDecayMs = 90.0f;
            p.synth[0].cutoffStart = p.synth[0].cutoffEnd = 1100.0f;
            p.synth[1].levelDb = -8.0f;
            makeRing (p.synth[2], 850.0f, 0.0f, 0.0f);
            p.synth[2].variant = pink;
            p.synth[2].ampShape = custom;
            p.synth[2].ampStart = 0.0f;
            p.synth[2].ampEnd = 1.0f;
            p.synth[2].ampPoints = { 0.12f, 1.0f, 0.4f, 0.0f };
            p.synth[2].cutoffStart = 600.0f;
            p.synth[2].cutoffEnd = 1500.0f;
            p.synth[2].width = 1.0f;
            p.synth[2].lfoRateStart = p.synth[2].lfoRateEnd = 5.0f;
            p.synth[2].lfoAmp = 0.25f;
            p.master.lowPassHz = 4500.0f;
            p.master.tiltDb = -2.0f;
            p.master.reverbType = 2.0f;     // Plate
            p.master.reverbMix = 0.5f;
            p.master.reverbSize = 0.9f;
            p.master.delayType = 1.0f;
            p.master.delayMix = 0.22f;
            p.master.delayFeedback = 0.45f;
        }));

        // ---- Sub Drop (2) -------------------------------------------------
        presets.push_back (make ("Sub Drop 01 Clean", FxType::subDrop));

        presets.push_back (make ("Sub Drop 02 Dirty", FxType::subDrop, [] (Patch& p)
        {
            p.synth[1].levelDb = -10.0f;
            p.synth[1].drive = 0.5f;
            p.synth[0].ampShape = custom;
            p.synth[0].ampPoints = { 1.0f, 0.95f, 0.8f, 0.6f };
            p.synth[0].ampStart = 0.0f;
            p.synth[0].ampEnd = 1.0f;
            p.macros = { 0.65f, 0.4f, 0.45f, 0.85f };
        }));

        presets.push_back (make ("Sub Drop 03 Long Fall", FxType::subDrop, [] (Patch& p)
        {
            p.synth[0].pitchStart = 48.0f;
            p.synth[0].pitchTension = -0.2f;
            p.synth[1].pitchStart = 48.0f;
            p.synth[1].pitchTension = -0.2f;
        }));

        // A kick-drum knock at the front, then a driven tone that dies away.
        presets.push_back (make ("Sub Drop 04 808 Dive", FxType::subDrop, [] (Patch& p)
        {
            p.synth[0].pitchStart = 24.0f;
            p.synth[0].pitchDropSt = 12.0f;
            p.synth[0].pitchDropMs = 30.0f;
            p.synth[0].ampEnd = 0.4f;
            p.synth[0].drive = 0.45f;
            p.synth[1].pitchStart = 24.0f;
        }));

        presets.push_back (make ("Sub Drop 05 Wobble", FxType::subDrop, [] (Patch& p)
        {
            p.synth[0].lfoPitchSt = 1.0f;
            p.synth[0].lfoAmp = 0.4f;
            p.synth[0].lfoRateStart = 5.0f;
            p.synth[0].lfoRateEnd = 2.0f;
        }));

        // More of the upper layer, so it is heard on small speakers too.
        presets.push_back (make ("Sub Drop 06 Octave Stack", FxType::subDrop, [] (Patch& p)
        {
            p.synth[1].levelDb = -8.0f;
            p.synth[1].cutoffStart = 2500.0f;
            p.synth[1].cutoffEnd = 500.0f;
        }));

        presets.push_back (make ("Sub Drop 07 Rumble", FxType::subDrop, [] (Patch& p)
        {
            auto& rumble = p.synth[2];
            rumble.enabled = 1.0f;
            rumble.type = 0.0f;             // Noise
            rumble.variant = brown;
            rumble.levelDb = -8.0f;
            rumble.filterType = lowPass;
            rumble.cutoffStart = 160.0f;
            rumble.cutoffEnd = 70.0f;
            rumble.resonance = 0.2f;
            rumble.ampStart = 1.0f;
            rumble.ampEnd = 0.6f;
            rumble.width = 0.3f;
        }));

        // ---- Empty -----------------------------------------------------
        // Nothing switched on: a clean start for playing samples only, or for
        // building a sound layer by layer.
        PresetData empty;
        empty.name = emptyPresetName;
        empty.patch.fxType = FxType::impact;
        presets.push_back (empty);

        return presets;
    }
}

const std::vector<PresetData>& getFactoryPresets()
{
    static const std::vector<PresetData> presets = build();
    return presets;
}

} // namespace fxtend
