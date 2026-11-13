#include "PresetSerializer.h"
#include "../engine/LayerFields.h"

namespace fxtend::PresetSerializer
{

namespace
{
    constexpr const char* fxTypeIds[numFxTypes] = { "impact", "riser", "downlifter", "sweep", "clap", "subDrop" };

    juce::var pointsToVar (const CurvePoints& points)
    {
        juce::Array<juce::var> values;

        for (float point : points)
            values.add ((double) point);

        return values;
    }

    void pointsFromVar (const juce::var& data, CurvePoints& points)
    {
        if (const auto* values = data.getArray(); values != nullptr && values->size() == (int) points.size())
            for (size_t i = 0; i < points.size(); ++i)
                points[i] = juce::jlimit (0.0f, 1.0f, (float) (double) values->getReference ((int) i));
    }

    float readNumber (const juce::var& object, const juce::Identifier& key, float fallback, float min, float max)
    {
        const auto& value = object[key];

        if (value.isBool())
            return (bool) value ? 1.0f : 0.0f;

        if (value.isInt() || value.isInt64() || value.isDouble())
        {
            const auto number = (float) (double) value;
            return std::isfinite (number) ? juce::jlimit (min, max, number) : fallback;
        }

        return fallback;
    }
}

const char* getFxTypeId (FxType type) noexcept
{
    return fxTypeIds[juce::jlimit (0, numFxTypes - 1, (int) type)];
}

juce::var sampleRefToVar (const SampleRef& ref)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("id", ref.id);
    object->setProperty ("fileName", ref.fileName);
    object->setProperty ("name", ref.name);
    object->setProperty ("originalPath", ref.originalPath);
    return juce::var (object);
}

SampleRef sampleRefFromVar (const juce::var& data)
{
    SampleRef ref;

    if (data.isObject())
    {
        ref.id = data["id"].toString();
        ref.fileName = data["fileName"].toString();
        ref.name = data["name"].toString();
        ref.originalPath = data["originalPath"].toString();
    }

    return ref;
}

namespace
{
    template <typename Params, typename FieldType>
    void writeFields (juce::DynamicObject& object, const Params& params, std::span<const FieldType> fields)
    {
        for (const auto& field : fields)
        {
            const float value = params.*(field.member);

            if (field.kind == FieldType::Kind::toggle)
                object.setProperty (field.id, value >= 0.5f);
            else if (field.kind == FieldType::Kind::continuous)
                object.setProperty (field.id, (double) value);
            else
                object.setProperty (field.id, juce::roundToInt (value));
        }
    }
}

juce::var toVar (const PresetData& preset)
{
    auto* root = new juce::DynamicObject();
    juce::var result (root);

    root->setProperty ("format", formatName);
    root->setProperty ("version", currentVersion);
    root->setProperty ("name", preset.name);
    root->setProperty ("fxType", getFxTypeId (preset.patch.fxType));
    root->setProperty ("tailMode", preset.patch.tailMode == TailMode::natural ? "natural" : "strict");
    root->setProperty ("masterGainDb", (double) preset.patch.masterGainDb);

    juce::Array<juce::var> macros;

    for (float macro : preset.patch.macros)
        macros.add ((double) macro);

    root->setProperty ("macros", macros);

    juce::Array<juce::var> layers;

    for (const auto& layer : preset.patch.synth)
    {
        auto* object = new juce::DynamicObject();

        for (const auto& field : getSynthLayerFields())
        {
            const float value = layer.*(field.member);

            if (field.kind == LayerField::Kind::toggle)
                object->setProperty (field.id, value >= 0.5f);
            else if (field.kind == LayerField::Kind::continuous)
                object->setProperty (field.id, (double) value);
            else
                object->setProperty (field.id, juce::roundToInt (value));
        }

        object->setProperty ("ampPoints", pointsToVar (layer.ampPoints));
        object->setProperty ("pitchPoints", pointsToVar (layer.pitchPoints));
        object->setProperty ("cutoffPoints", pointsToVar (layer.cutoffPoints));
        layers.add (juce::var (object));
    }

    root->setProperty ("synth", layers);

    juce::Array<juce::var> samples;

    for (size_t slot = 0; slot < preset.patch.samples.size(); ++slot)
    {
        auto* object = new juce::DynamicObject();
        writeFields (*object, preset.patch.samples[slot], getSampleLayerFields());

        if (! preset.sampleRefs[slot].isEmpty())
            object->setProperty ("file", sampleRefToVar (preset.sampleRefs[slot]));

        samples.add (juce::var (object));
    }

    root->setProperty ("samples", samples);

    auto* master = new juce::DynamicObject();

    for (const auto& field : getMasterFields())
    {
        const float value = preset.patch.master.*(field.member);

        if (field.kind == MasterField::Kind::toggle)
            master->setProperty (field.id, value >= 0.5f);
        else if (field.kind == MasterField::Kind::continuous)
            master->setProperty (field.id, (double) value);
        else
            master->setProperty (field.id, juce::roundToInt (value));
    }

    root->setProperty ("master", juce::var (master));
    return result;
}

juce::String toJson (const PresetData& preset)
{
    return juce::JSON::toString (toVar (preset));
}

juce::Result fromVar (const juce::var& data, PresetData& preset)
{
    if (! data.isObject() || data["format"].toString() != formatName)
        return juce::Result::fail ("This is not an FXtend preset.");

    if ((int) data["version"] > currentVersion)
        return juce::Result::fail ("This preset was made with a newer version of FXtend.");

    PresetData loaded;
    loaded.name = data["name"].toString();

    const auto typeId = data["fxType"].toString();

    for (int i = 0; i < numFxTypes; ++i)
        if (typeId == fxTypeIds[i])
            loaded.patch.fxType = (FxType) i;

    loaded.patch.tailMode = data["tailMode"].toString() == "natural" ? TailMode::natural : TailMode::strict;
    loaded.patch.masterGainDb = readNumber (data, "masterGainDb", 0.0f, -24.0f, 12.0f);

    if (const auto* macros = data["macros"].getArray())
        for (int i = 0; i < juce::jmin (numMacros, macros->size()); ++i)
            loaded.patch.macros[(size_t) i] = juce::jlimit (0.0f, 1.0f, (float) (double) macros->getReference (i));

    if (const auto* layers = data["synth"].getArray())
    {
        for (int i = 0; i < juce::jmin (numSynthLayers, layers->size()); ++i)
        {
            const auto& object = layers->getReference (i);
            auto& layer = loaded.patch.synth[(size_t) i];

            if (! object.isObject())
                continue;

            for (const auto& field : getSynthLayerFields())
                layer.*(field.member) = readNumber (object, field.id, getDefaultValue (field), field.min, field.max);

            pointsFromVar (object["ampPoints"], layer.ampPoints);
            pointsFromVar (object["pitchPoints"], layer.pitchPoints);
            pointsFromVar (object["cutoffPoints"], layer.cutoffPoints);
        }
    }

    if (const auto* samples = data["samples"].getArray())
    {
        for (int i = 0; i < juce::jmin (numSampleLayers, samples->size()); ++i)
        {
            const auto& object = samples->getReference (i);

            if (! object.isObject())
                continue;

            for (const auto& field : getSampleLayerFields())
                loaded.patch.samples[(size_t) i].*(field.member) = readNumber (object, field.id, getDefaultValue (field), field.min, field.max);

            loaded.sampleRefs[(size_t) i] = sampleRefFromVar (object["file"]);
        }
    }

    if (const auto& master = data["master"]; master.isObject())
        for (const auto& field : getMasterFields())
            loaded.patch.master.*(field.member) = readNumber (master, field.id, getDefaultValue (field), field.min, field.max);

    preset = loaded;
    return juce::Result::ok();
}

juce::Result fromJson (const juce::String& json, PresetData& preset)
{
    juce::var data;
    const auto result = juce::JSON::parse (json, data);

    if (result.failed())
        return juce::Result::fail ("The preset file could not be read: " + result.getErrorMessage());

    return fromVar (data, preset);
}

} // namespace fxtend::PresetSerializer
