#pragma once

#include "PresetSerializer.h"

#include <vector>

namespace fxtend
{

/** The one factory preset with every synth layer switched off: a clean start
    for playing samples only. Silent until a sample is loaded or a layer is turned on. */
inline constexpr const char* emptyPresetName = "Init (Empty)";

/** The built-in presets. All of them are pure synthesis: no audio files needed. */
const std::vector<PresetData>& getFactoryPresets();

} // namespace fxtend
