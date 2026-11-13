#include "FieldControl.h"

namespace fxui
{

FieldControl::FieldControl (juce::AudioProcessorValueTreeState& state, const Description& description)
    : field (description)
{
    parameter = state.getParameter (field.parameterId);
    jassert (parameter != nullptr);

    title.setText (field.name, juce::dontSendNotification);
    title.setFont (font (10.5f));
    title.setColour (juce::Label::textColourId, Colours::dimText);
    title.setInterceptsMouseClicks (false, false);

    if (field.isToggle)
    {
        toggle.setButtonText (field.name);
        addAndMakeVisible (toggle);
        buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, field.parameterId, toggle);
    }
    else if (field.hasDynamicChoices)
    {
        addAndMakeVisible (title);
        addAndMakeVisible (box);

        box.onChange = [this]
        {
            if (dynamicAttachment != nullptr && box.getSelectedId() > 0)
                dynamicAttachment->setValueAsCompleteGesture ((float) (box.getSelectedId() - 1));
        };

        dynamicAttachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float value)
        {
            showDynamicChoice (juce::roundToInt (value));
        });
    }
    else if (field.isChoice)
    {
        addAndMakeVisible (title);
        box.addItemList (juce::StringArray::fromTokens (field.choices, "|", ""), 1);
        addAndMakeVisible (box);
        boxAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, field.parameterId, box);
    }
    else
    {
        addAndMakeVisible (title);
        slider.setSliderStyle (juce::Slider::LinearBar);
        slider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
        addAndMakeVisible (slider);
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, field.parameterId, slider);
        slider.setTextValueSuffix (field.unit.isEmpty() ? juce::String() : " " + field.unit);
    }
}

void FieldControl::setChoices (const juce::StringArray& names, const juce::String& caption)
{
    if (! field.hasDynamicChoices)
        return;

    box.clear (juce::dontSendNotification);
    box.addItemList (names, 1);

    if (parameter != nullptr && ! names.isEmpty())
        showDynamicChoice (juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())));

    title.setText (caption, juce::dontSendNotification);
}

void FieldControl::showDynamicChoice (int index)
{
    // The same setting means different things for different layer types, and a
    // value left over from a type with more choices (Noise: Brown) can be out
    // of range for this one. The engine plays such a value as the first choice,
    // so that is what is shown.
    const int shown = juce::isPositiveAndBelow (index, box.getNumItems()) ? index : 0;
    box.setSelectedId (shown + 1, juce::dontSendNotification);
}

void FieldControl::setRelevant (bool isRelevant)
{
    setAlpha (isRelevant ? 1.0f : 0.3f);
    setEnabled (isRelevant);
}

void FieldControl::resized()
{
    auto bounds = getLocalBounds().reduced (4, 3);

    if (field.isToggle)
    {
        toggle.setBounds (bounds.withTrimmedTop (14));
        return;
    }

    title.setBounds (bounds.removeFromTop (14));
    box.setBounds (bounds.withHeight (juce::jmin (24, bounds.getHeight())));
    slider.setBounds (bounds.withHeight (juce::jmin (24, bounds.getHeight())));
}

} // namespace fxui
