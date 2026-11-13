#pragma once

#include "../engine/Patch.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace fxui
{

namespace Colours
{
    const juce::Colour background { 0xff14161b };
    const juce::Colour panel      { 0xff1c1f26 };
    const juce::Colour control    { 0xff2a2e38 };
    const juce::Colour outline    { 0xff3a3f4b };
    const juce::Colour text       { 0xffe6e9ef };
    const juce::Colour dimText    { 0xff8d94a3 };
    const juce::Colour warning    { 0xffffc857 };
    const juce::Colour danger     { 0xffff5c5c };
}

/** Each FX type has its own accent so the current type is obvious at a glance. */
juce::Colour getAccent (fxtend::FxType type) noexcept;

class Theme final : public juce::LookAndFeel_V4
{
public:
    Theme();

    void setAccent (juce::Colour newAccent);
    juce::Colour getAccent() const noexcept { return accent; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float minPosition, float maxPosition, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;

private:
    juce::Colour accent { 0xffff8a3d };
};

/** A rounder, friendlier face than the default (ships with macOS). */
inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions().withName ("Avenir Next").withStyle (bold ? "Demi Bold" : "Medium").withHeight (size * 1.06f));
}

/** The soft card every section of the editor sits on. */
void paintPanel (juce::Graphics&, juce::Rectangle<float> bounds);

/** A small picture of what an FX type sounds like over time (its loudness
    shape), used on the type buttons. */
void drawTypeGlyph (juce::Graphics&, juce::Rectangle<float> area, fxtend::FxType type, juce::Colour colour);

} // namespace fxui
