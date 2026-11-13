#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace fxui
{

/** Stereo output peak meter with a clip light. Click the light to clear it. */
class MeterView final : public juce::Component
{
public:
    MeterView() = default;

    /** Call regularly from a timer with the peaks of the latest audio block. */
    void update (float peakLeft, float peakRight, int clipCount)
    {
        const float newLeft = juce::jmax (peakLeft, levelLeft * decay);
        const float newRight = juce::jmax (peakRight, levelRight * decay);
        const bool newClipped = clipCount != acknowledgedClipCount;

        if (std::abs (newLeft - levelLeft) > 1.0e-4f || std::abs (newRight - levelRight) > 1.0e-4f || newClipped != clipped)
        {
            levelLeft = newLeft < 1.0e-4f ? 0.0f : newLeft;
            levelRight = newRight < 1.0e-4f ? 0.0f : newRight;
            clipped = newClipped;
            repaint();
        }

        latestClipCount = clipCount;
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        acknowledgedClipCount = latestClipCount;
        clipped = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        auto light = bounds.removeFromTop (12.0f).reduced (1.0f, 2.0f);
        bounds.removeFromTop (3.0f);

        g.setColour (clipped ? Colours::danger : Colours::control);
        g.fillRoundedRectangle (light, 2.0f);

        const float barWidth = (bounds.getWidth() - 3.0f) * 0.5f;
        drawBar (g, bounds.removeFromLeft (barWidth), levelLeft);
        bounds.removeFromLeft (3.0f);
        drawBar (g, bounds, levelRight);
    }

private:
    static void drawBar (juce::Graphics& g, juce::Rectangle<float> area, float level)
    {
        g.setColour (Colours::control);
        g.fillRoundedRectangle (area, 2.0f);

        // -60 dB at the bottom, 0 dB at the top.
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

        if (proportion > 0.0f)
        {
            g.setColour (db > -1.0f ? Colours::danger : (db > -9.0f ? Colours::warning : juce::Colour (0xff6fd08c)));
            g.fillRoundedRectangle (area.withTop (area.getBottom() - area.getHeight() * proportion), 2.0f);
        }
    }

    static constexpr float decay = 0.86f;

    float levelLeft = 0.0f, levelRight = 0.0f;
    bool clipped = false;
    int latestClipCount = 0, acknowledgedClipCount = 0;
};

} // namespace fxui
