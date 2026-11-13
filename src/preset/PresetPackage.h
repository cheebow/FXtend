#pragma once

#include "PresetSerializer.h"
#include "../sample/SampleManager.h"

namespace fxtend
{

/** A preset together with the sample files it uses, in one file that can be
    moved to another computer. It is a zip holding preset.json and samples/. */
namespace PresetPackage
{
    inline constexpr const char* fileExtension = ".fxtendpack";

    /** Fails if one of the preset's samples is missing on this computer. */
    juce::Result exportPackage (const PresetData& preset, const SampleManager& samples, const juce::File& target);

    /** Reads the preset and puts its samples into the managed sample folder.
        Sample files are only accepted if their contents match the hash the
        preset refers to. */
    juce::Result importPackage (const juce::File& package, const SampleManager& samples, PresetData& preset);
}

} // namespace fxtend
