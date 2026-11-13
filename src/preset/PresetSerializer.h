#pragma once

#include "../engine/Patch.h"
#include "../sample/SampleRef.h"

#include <juce_core/juce_core.h>

namespace fxtend
{

/** A named sound as stored in a preset file. Length is deliberately not part
    of a preset: it belongs to the song, so browsing presets keeps it. */
struct PresetData
{
    juce::String name;
    Patch patch;

    /** Which file each sample slot uses (empty id = empty slot). The audio
        itself is not part of a preset. */
    std::array<SampleRef, numSampleLayers> sampleRefs;

    bool usesSamples() const noexcept
    {
        for (const auto& ref : sampleRefs)
            if (! ref.isEmpty())
                return true;

        return false;
    }
};

namespace PresetSerializer
{
    inline constexpr int currentVersion = 1;
    inline constexpr const char* formatName = "fxtend-preset";
    inline constexpr const char* fileExtension = ".fxpreset";

    juce::var toVar (const PresetData& preset);
    juce::String toJson (const PresetData& preset);

    /** Missing values keep their defaults, so older files load in newer versions.
        Fails for files that are not presets or come from a newer format version. */
    juce::Result fromVar (const juce::var& data, PresetData& preset);
    juce::Result fromJson (const juce::String& json, PresetData& preset);

    const char* getFxTypeId (FxType type) noexcept;

    juce::var sampleRefToVar (const SampleRef& ref);
    SampleRef sampleRefFromVar (const juce::var& data);
}

} // namespace fxtend
