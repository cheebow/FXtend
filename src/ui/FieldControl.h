#pragma once

#include "Theme.h"
#include "../Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>

namespace fxui
{

/** One labelled control bound to a host parameter: a bar slider, a choice box
    or a tick box, depending on the kind of setting. */
class FieldControl final : public juce::Component
{
public:
    /** What a control needs to know about its setting, whichever table it comes from. */
    struct Description
    {
        juce::String id, name, unit, choices, parameterId;
        bool isToggle = false, isChoice = false;

        /** Optional: for settings whose choices depend on something else
            (the synth layer "variant"). When set, the control is a choice box
            whose list is filled by setChoices(). */
        bool hasDynamicChoices = false;
    };

    template <typename FieldType>
    static Description describe (const FieldType& source, const juce::String& parameterId)
    {
        return { source.id, source.name, source.unit, source.choices, parameterId,
                 source.kind == FieldType::Kind::toggle, source.kind == FieldType::Kind::choice, false };
    }

    FieldControl (juce::AudioProcessorValueTreeState& state, const Description& description);

    /** For dynamic-choice controls: replaces the list and the caption. */
    void setChoices (const juce::StringArray& names, const juce::String& caption);

    /** Greys the control out when its setting has no effect at the moment. */
    void setRelevant (bool isRelevant);

    void resized() override;

    /** A control built while a page is open is created under the default look;
        this makes it take the editor's look as soon as it is put on screen. */
    void parentHierarchyChanged() override   { sendLookAndFeelChange(); }

    const Description field;

private:
    juce::RangedAudioParameter* parameter = nullptr;
    juce::Label title;
    juce::Slider slider;
    juce::ComboBox box;
    juce::ToggleButton toggle;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> boxAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    std::unique_ptr<juce::ParameterAttachment> dynamicAttachment;
};

/** A grid of FieldControls: rows with a title on the left, a fixed number of columns. */
struct FieldRow
{
    const char* title;
    std::array<const char*, 7> ids;     // empty strings leave a gap
};

inline constexpr int fieldColumns = 7;
inline constexpr int fieldRowHeight = 46;
inline constexpr int fieldTitleWidth = 54;

} // namespace fxui
