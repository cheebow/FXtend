#include "Parameters.h"

using namespace fxtend;

namespace
{
    constexpr int parameterVersion = 1;

    juce::ParameterID pid (const juce::String& id)      { return { id, parameterVersion }; }

    juce::StringArray splitChoices (const char* choices)
    {
        return juce::StringArray::fromTokens (choices, "|", "");
    }

    juce::NormalisableRange<float> makeRange (float min, float max, float centre)
    {
        juce::NormalisableRange<float> range (min, max);

        if (centre > min && centre < max)
            range.setSkewForCentre (centre);

        return range;
    }

    juce::String formatValue (float value, int)
    {
        const float magnitude = std::abs (value);
        return juce::String (value, magnitude >= 100.0f ? 0 : (magnitude >= 10.0f ? 1 : 2));
    }

    juce::AudioParameterFloatAttributes floatAttributes (const juce::String& unit)
    {
        return juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (formatValue);
    }

    void setParameter (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
    {
        if (auto* parameter = state.getParameter (id))
        {
            const float normalised = parameter->convertTo0to1 (value);

            if (std::abs (parameter->getValue() - normalised) > 1.0e-7f)
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (normalised);
                parameter->endChangeGesture();
            }
        }
    }
}

juce::String ParamIDs::macro (int index)
{
    return "macro" + juce::String (index + 1);
}

juce::String ParamIDs::synthLayer (int layer, const LayerField& field)
{
    return "s" + juce::String (layer + 1) + "_" + field.id;
}

juce::String ParamIDs::sampleLayer (int slot, const SampleField& field)
{
    return "p" + juce::String (slot + 1) + "_" + field.id;
}

juce::String ParamIDs::master (const MasterField& field)
{
    return juce::String ("m_") + field.id;
}

namespace
{
    template <typename FieldType>
    std::unique_ptr<juce::RangedAudioParameter> makeFieldParameter (const juce::String& id, const juce::String& name, const FieldType& field)
    {
        using Kind = typename FieldType::Kind;
        const float defaultValue = getDefaultValue (field);

        switch (field.kind)
        {
            case Kind::toggle:
                return std::make_unique<juce::AudioParameterBool> (pid (id), name, defaultValue >= 0.5f);

            case Kind::choice:
                return std::make_unique<juce::AudioParameterChoice> (pid (id), name, splitChoices (field.choices), juce::roundToInt (defaultValue));

            case Kind::integer:
                return std::make_unique<juce::AudioParameterInt> (pid (id), name, juce::roundToInt (field.min),
                                                                  juce::roundToInt (field.max), juce::roundToInt (defaultValue));

            case Kind::continuous:
                break;
        }

        return std::make_unique<juce::AudioParameterFloat> (pid (id), name, makeRange (field.min, field.max, field.centre),
                                                            defaultValue, floatAttributes (field.unit));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using Float = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Choice> (pid (ParamIDs::fxType), "FX Type", splitChoices (fxTypeChoices), (int) FxType::riser));
    layout.add (std::make_unique<Choice> (pid (ParamIDs::lengthUnit), "Length Unit", splitChoices (lengthUnitChoices), 0));
    layout.add (std::make_unique<Float> (pid (ParamIDs::lengthValue), "Length",
                                         juce::NormalisableRange<float> (0.25f, 32.0f, 0.25f), 4.0f, floatAttributes ({})));
    layout.add (std::make_unique<Choice> (pid (ParamIDs::tailMode), "Tail Mode", splitChoices (tailModeChoices), 0));
    layout.add (std::make_unique<Choice> (pid (ParamIDs::retrigger), "Retrigger", splitChoices (retriggerChoices), 0));
    layout.add (std::make_unique<Float> (pid (ParamIDs::manualBpm), "Manual BPM",
                                         juce::NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f,
                                         floatAttributes ("BPM")));
    layout.add (std::make_unique<Float> (pid (ParamIDs::masterGain), "Master Gain",
                                         juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                         floatAttributes ("dB")));

    for (int i = 0; i < numMacros; ++i)
        layout.add (std::make_unique<Float> (pid (ParamIDs::macro (i)), "Macro " + juce::String (i + 1),
                                             juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, floatAttributes ({})));

    for (int layer = 0; layer < numSynthLayers; ++layer)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("synth" + juce::String (layer + 1),
                                                                           "Synth " + juce::String (layer + 1), " | ");
        const juce::String prefix = "S" + juce::String (layer + 1) + " ";

        for (const auto& field : getSynthLayerFields())
            group->addChild (makeFieldParameter (ParamIDs::synthLayer (layer, field), prefix + field.name, field));

        layout.add (std::move (group));
    }

    for (int slot = 0; slot < numSampleLayers; ++slot)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("sample" + juce::String (slot + 1),
                                                                           "Sample " + juce::String (slot + 1), " | ");
        const juce::String prefix = "P" + juce::String (slot + 1) + " ";

        for (const auto& field : getSampleLayerFields())
            group->addChild (makeFieldParameter (ParamIDs::sampleLayer (slot, field), prefix + field.name, field));

        layout.add (std::move (group));
    }

    auto masterGroup = std::make_unique<juce::AudioProcessorParameterGroup> ("master", "Master", " | ");

    for (const auto& field : getMasterFields())
        masterGroup->addChild (makeFieldParameter (ParamIDs::master (field), juce::String ("Master ") + field.name, field));

    layout.add (std::move (masterGroup));
    return layout;
}

ParameterReader::ParameterReader (juce::AudioProcessorValueTreeState& state)
{
    fxType = state.getRawParameterValue (ParamIDs::fxType);
    lengthUnit = state.getRawParameterValue (ParamIDs::lengthUnit);
    lengthValue = state.getRawParameterValue (ParamIDs::lengthValue);
    tailMode = state.getRawParameterValue (ParamIDs::tailMode);
    retrigger = state.getRawParameterValue (ParamIDs::retrigger);
    manualBpm = state.getRawParameterValue (ParamIDs::manualBpm);
    masterGain = state.getRawParameterValue (ParamIDs::masterGain);

    for (int i = 0; i < numMacros; ++i)
        macros[(size_t) i] = state.getRawParameterValue (ParamIDs::macro (i));

    for (int layer = 0; layer < numSynthLayers; ++layer)
        for (const auto& field : getSynthLayerFields())
            layers[(size_t) layer].push_back (state.getRawParameterValue (ParamIDs::synthLayer (layer, field)));

    for (int slot = 0; slot < numSampleLayers; ++slot)
        for (const auto& field : getSampleLayerFields())
            sampleValues[(size_t) slot].push_back (state.getRawParameterValue (ParamIDs::sampleLayer (slot, field)));

    for (const auto& field : getMasterFields())
        masterValues.push_back (state.getRawParameterValue (ParamIDs::master (field)));
}

void ParameterReader::read (Patch& patch, EngineSettings& settings) const noexcept
{
    const auto load = [] (const std::atomic<float>* value) { return value->load (std::memory_order_relaxed); };

    patch.fxType = (FxType) juce::jlimit (0, numFxTypes - 1, juce::roundToInt (load (fxType)));
    patch.tailMode = load (tailMode) >= 0.5f ? TailMode::natural : TailMode::strict;
    patch.masterGainDb = load (masterGain);

    for (size_t i = 0; i < macros.size(); ++i)
        patch.macros[i] = load (macros[i]);

    const auto fields = getSynthLayerFields();

    for (size_t layer = 0; layer < layers.size(); ++layer)
        for (size_t i = 0; i < fields.size(); ++i)
            patch.synth[layer].*(fields[i].member) = load (layers[layer][i]);

    const auto sampleFields = getSampleLayerFields();

    for (size_t slot = 0; slot < sampleValues.size(); ++slot)
        for (size_t i = 0; i < sampleFields.size(); ++i)
            patch.samples[slot].*(sampleFields[i].member) = load (sampleValues[slot][i]);

    const auto masterFields = getMasterFields();

    for (size_t i = 0; i < masterFields.size(); ++i)
        patch.master.*(masterFields[i].member) = load (masterValues[i]);

    settings.lengthUnit = (LengthUnit) juce::jlimit (0, 2, juce::roundToInt (load (lengthUnit)));
    settings.lengthValue = load (lengthValue);
    settings.retrigger = load (retrigger) >= 0.5f ? RetriggerMode::overlap : RetriggerMode::restart;
    settings.manualBpm = load (manualBpm);
}

void applyPatchToParameters (juce::AudioProcessorValueTreeState& state, const Patch& patch)
{
    setParameter (state, ParamIDs::fxType, (float) (int) patch.fxType);
    setParameter (state, ParamIDs::tailMode, (float) (int) patch.tailMode);
    setParameter (state, ParamIDs::masterGain, patch.masterGainDb);

    for (int i = 0; i < numMacros; ++i)
        setParameter (state, ParamIDs::macro (i), patch.macros[(size_t) i]);

    for (int layer = 0; layer < numSynthLayers; ++layer)
        for (const auto& field : getSynthLayerFields())
            setParameter (state, ParamIDs::synthLayer (layer, field), patch.synth[(size_t) layer].*(field.member));

    for (int slot = 0; slot < numSampleLayers; ++slot)
        for (const auto& field : getSampleLayerFields())
            setParameter (state, ParamIDs::sampleLayer (slot, field), patch.samples[(size_t) slot].*(field.member));

    for (const auto& field : getMasterFields())
        setParameter (state, ParamIDs::master (field), patch.master.*(field.member));
}
