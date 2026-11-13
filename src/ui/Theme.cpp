#include "Theme.h"

namespace fxui
{

juce::Colour getAccent (fxtend::FxType type) noexcept
{
    switch (type)
    {
        case fxtend::FxType::impact:       return juce::Colour (0xffff6b5e);
        case fxtend::FxType::riser:        return juce::Colour (0xffffa53d);
        case fxtend::FxType::downlifter:   return juce::Colour (0xff5eb1ff);
        case fxtend::FxType::sweep:        return juce::Colour (0xff4fd6c4);
        case fxtend::FxType::clap:         return juce::Colour (0xffd78bff);
        case fxtend::FxType::subDrop:      return juce::Colour (0xff8ee06b);
    }

    return juce::Colour (0xffffa53d);
}

void paintPanel (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setGradientFill (juce::ColourGradient (Colours::panel.brighter (0.05f), 0.0f, bounds.getY(),
                                             Colours::panel.darker (0.08f), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 9.0f);
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 9.0f, 1.0f);
}

void drawTypeGlyph (juce::Graphics& g, juce::Rectangle<float> area, fxtend::FxType type, juce::Colour colour)
{
    using fxtend::FxType;
    g.setColour (colour);

    if (type == FxType::subDrop)
    {
        // A low tone sliding down: a wave that gets slower.
        juce::Path wave;

        for (int i = 0; i <= 48; ++i)
        {
            const float x = (float) i / 48.0f;
            const float y = std::sin (6.2831853f * (3.2f * x - 1.5f * x * x)) * (1.0f - 0.25f * x);
            const juce::Point<float> point (area.getX() + x * area.getWidth(), area.getCentreY() - y * area.getHeight() * 0.42f);

            if (i == 0)
                wave.startNewSubPath (point);
            else
                wave.lineTo (point);
        }

        g.strokePath (wave, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    // Loudness over time, drawn as bars mirrored around the middle.
    const auto loudness = [type] (float x)
    {
        switch (type)
        {
            case FxType::impact:        return std::exp (-3.6f * x);
            case FxType::riser:         return 0.1f + 0.9f * x * x;
            case FxType::downlifter:    return 0.1f + 0.9f * (1.0f - x) * (1.0f - x);
            case FxType::sweep:         return 0.12f + 0.88f * std::sin (3.1415927f * x) * std::sin (3.1415927f * x);
            case FxType::clap:          return x < 0.5f ? (((int) (x * 12.0f)) % 2 == 0 ? 1.0f : 0.12f) : 0.75f * std::exp (-5.0f * (x - 0.5f));
            case FxType::subDrop:       break;
        }

        return 0.5f;
    };

    const int bars = 9;
    const float step = area.getWidth() / (float) bars;

    for (int i = 0; i < bars; ++i)
    {
        const float height = juce::jmax (2.0f, loudness ((float) i / (float) (bars - 1)) * area.getHeight());
        g.fillRoundedRectangle (area.getX() + (float) i * step, area.getCentreY() - height * 0.5f, step * 0.62f, height, step * 0.3f);
    }
}

Theme::Theme()
{
    setDefaultSansSerifTypefaceName ("Avenir Next");
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::TextButton::buttonColourId, Colours::control);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::background);
    setColour (juce::ComboBox::backgroundColourId, Colours::control);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::ComboBox::arrowColourId, Colours::dimText);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::headerTextColourId, Colours::dimText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::background);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
    setColour (juce::TextEditor::backgroundColourId, Colours::control);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, Colours::text);
    setColour (juce::ToggleButton::textColourId, Colours::text);
}

void Theme::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
}

void Theme::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                              float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float thickness = juce::jmax (3.0f, radius * 0.16f);
    const float arcRadius = radius - thickness * 0.5f;
    const float angle = startAngle + position * (endAngle - startAngle);
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::control.withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar controls fill from the middle, others from the start.
    const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
    const float fromAngle = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setColour (accent.withAlpha (0.16f * alpha));     // a soft glow under the value
    g.strokePath (value, juce::PathStrokeType (thickness * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent.withMultipliedAlpha (alpha));
    g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobRadius = arcRadius - thickness * 1.1f;
    const auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (knob.translated (0.0f, 2.0f));
    g.setGradientFill (juce::ColourGradient (Colours::control.brighter (0.22f), 0.0f, knob.getY(),
                                             Colours::panel.brighter (0.02f), 0.0f, knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (knob.reduced (0.5f), 1.0f);

    const juce::Point<float> tip (centre.x + knobRadius * 0.82f * std::sin (angle), centre.y - knobRadius * 0.82f * std::cos (angle));
    const juce::Point<float> base (centre.x + knobRadius * 0.35f * std::sin (angle), centre.y - knobRadius * 0.35f * std::cos (angle));
    g.setColour (Colours::text.withMultipliedAlpha (alpha));
    g.drawLine ({ base, tip }, 2.0f);
}

void Theme::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                              float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearVertical)
    {
        // Mixer fader: a slim track filled from the bottom and a cap to grab.
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        const float centreX = (float) x + (float) width * 0.5f;
        const auto track = juce::Rectangle<float> (centreX - 3.0f, (float) y, 6.0f, (float) height);

        g.setColour (Colours::control.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (track, 3.0f);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.9f * alpha), 0.0f, position,
                                                 accent.withAlpha (0.3f * alpha), 0.0f, track.getBottom(), false));
        g.fillRoundedRectangle (track.withTop (position), 3.0f);

        const auto cap = juce::Rectangle<float> (34.0f, 14.0f).withCentre ({ centreX, position });
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 4.0f);
        g.setGradientFill (juce::ColourGradient (Colours::text, 0.0f, cap.getY(), Colours::dimText, 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (accent);
        g.fillRect (cap.reduced (5.0f, 6.0f));
        return;
    }

    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, position, 0.0f, 0.0f, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;

    g.setColour (Colours::control.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bounds, 5.0f);

    g.saveState();
    g.reduceClipRegion (bounds.withRight (position).toNearestInt());
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f * alpha), bounds.getX(), 0.0f,
                                             accent.withAlpha (0.55f * alpha), juce::jmax (bounds.getX() + 1.0f, position), 0.0f, false));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.restoreState();

    // A bright tick where the value is, so the bar reads as something to drag.
    if (position > bounds.getX() + 2.0f && position < bounds.getRight() - 2.0f)
    {
        g.setColour (accent.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (position - 1.5f, bounds.getBottom() - 6.0f, 3.0f, 5.0f, 1.2f);
    }
}

void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = button.getToggleState() ? accent : Colours::control;

    if (down)
        colour = colour.brighter (0.2f);
    else if (highlighted)
        colour = colour.brighter (0.08f);

    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.4f);

    g.setGradientFill (juce::ColourGradient (colour.brighter (0.1f), 0.0f, bounds.getY(), colour.darker (0.08f), 0.0f, bounds.getBottom(), false));

    if (button.getConnectedEdgeFlags() != 0)
    {
        // Part of a segmented switch (the page tabs): only the outer corners are round.
        juce::Path shape;
        const bool left = ! button.isConnectedOnLeft(), right = ! button.isConnectedOnRight();
        shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), 7.0f, 7.0f, left, right, left, right);
        g.fillPath (shape);
        return;
    }

    g.fillRoundedRectangle (bounds, 7.0f);

    if (! button.getToggleState())
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);
    }
}

void Theme::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                            .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);
    const auto textFont = getTextButtonFont (button, button.getHeight());
    auto bounds = button.getLocalBounds().toFloat().reduced (6.0f, 0.0f);

    // FX type buttons carry a picture of the type next to their name.
    if (const auto* type = button.getProperties().getVarPointer ("fxType"))
    {
        const float glyphWidth = 26.0f, gap = 9.0f;
        const float textWidth = juce::GlyphArrangement::getStringWidth (textFont, button.getButtonText());
        const float total = juce::jmin (bounds.getWidth(), glyphWidth + gap + textWidth);
        auto content = bounds.withSizeKeepingCentre (total, bounds.getHeight());
        const auto fxType = (fxtend::FxType) (int) *type;

        drawTypeGlyph (g, content.removeFromLeft (glyphWidth).withSizeKeepingCentre (glyphWidth, 16.0f), fxType,
                       button.getToggleState() ? colour : fxui::getAccent (fxType));
        content.removeFromLeft (gap);
        bounds = content;
    }

    g.setColour (colour);
    g.setFont (textFont);
    g.drawFittedText (button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void Theme::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto box = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ 10.0f, bounds.getCentreY() });

    g.setColour (button.getToggleState() ? accent : Colours::control.brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (box, 5.0f);

    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
        tick.lineTo (box.getCentreX() - 1.0f, box.getBottom() - 4.0f);
        tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.0f);
        g.setColour (Colours::background);
        g.strokePath (tick, juce::PathStrokeType (2.0f));
    }

    g.setColour (Colours::text.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.setFont (font (13.0f));
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
}

void Theme::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    const float alpha = box.isEnabled() ? 1.0f : 0.4f;

    g.setColour (Colours::control.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bounds, 6.0f);

    juce::Path arrow;
    const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (Colours::dimText.withMultipliedAlpha (alpha));
    g.fillPath (arrow);
}

juce::Label* Theme::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (font (slider.getSliderStyle() == juce::Slider::LinearBar ? 14.0f : 15.0f));

    // Set here rather than looked up: a control built before it is on screen
    // would otherwise pick up the default look's outline and colours.
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::textColourId, Colours::text);
    label->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    return label;
}

juce::Font Theme::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jmin (14.0f, (float) buttonHeight * 0.55f));
}

juce::Font Theme::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jmin (14.0f, (float) box.getHeight() * 0.6f));
}

juce::Font Theme::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

} // namespace fxui
