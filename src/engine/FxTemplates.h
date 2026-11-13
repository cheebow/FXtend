#pragma once

#include "Patch.h"

namespace fxtend
{

/** Name shown for an FX type. */
const char* getFxTypeName (FxType type) noexcept;

/** Label of one of the type's macro knobs. */
const char* getMacroName (FxType type, int macroIndex) noexcept;

/** The starting-point layer setup for an FX type, with all macros neutral. */
Patch makeTemplate (FxType type);

/** Returns the patch the engine should actually play: the stored layers with
    the FX type's macro knobs applied. A macro at 0.5 changes nothing. */
Patch applyMacros (const Patch& patch) noexcept;

} // namespace fxtend
