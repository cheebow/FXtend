#pragma once

#include "Patch.h"

#include <span>

namespace fxtend
{

/** Describes one float member of a parameter struct: its stable id, range and default.

    These tables drive the host parameters, preset files and the editor. Ids and
    choice orders are part of the saved-state format: add new entries and new
    choices at the end, never rename or reorder. */
template <typename Owner>
struct Field
{
    enum class Kind { continuous, toggle, choice, integer };

    const char* id;                         // stable, used in parameter ids and presets
    const char* name;                       // shown to the user
    float Owner::* member;
    Kind kind;
    float min;
    float max;
    float centre;                           // value at the middle of the control; 0 = linear
    const char* unit;
    const char* choices;                    // '|' separated, for Kind::choice
};

using LayerField = Field<SynthLayerParams>;
using MasterField = Field<MasterParams>;
using SampleField = Field<SampleLayerParams>;

std::span<const LayerField> getSynthLayerFields() noexcept;
std::span<const MasterField> getMasterFields() noexcept;
std::span<const SampleField> getSampleLayerFields() noexcept;

/** Default value of a field (from a default-constructed struct). */
float getDefaultValue (const LayerField& field) noexcept;
float getDefaultValue (const MasterField& field) noexcept;
float getDefaultValue (const SampleField& field) noexcept;

/** Delay times offered by the master delay, in quarter notes. */
inline constexpr float delayTimesQn[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
inline constexpr const char* delayTimeChoices = "1/16|1/8|1/8 dotted|1/4|1/4 dotted|1/2";

inline constexpr const char* fxTypeChoices = "Impact|Riser|Downlifter|Sweep|Clap|Sub Drop";
inline constexpr const char* lengthUnitChoices = "Bars|Beats|Quarter Notes";
inline constexpr const char* tailModeChoices = "Strict|Natural";
inline constexpr const char* retriggerChoices = "Restart|Overlap";
inline constexpr const char* stoppedNotesChoices = "Full|Gate|Off";

} // namespace fxtend
