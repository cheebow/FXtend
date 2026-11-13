#pragma once

#include "Theme.h"
#include "../engine/FxEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>

namespace fxui
{

/** Shows how wide the output is.

    The cloud is the recent output with "both speakers the same" drawn straight
    up and "only one speaker" leaning 45 degrees to that side: a thin vertical
    line is mono, a round cloud is wide. The bar underneath is the correlation:
    right (+1) means mono-compatible, left (-1) means the channels cancel. */
class StereoScope final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Timer
{
public:
    explicit StereoScope (const fxtend::EngineStatus& statusToShow) : status (statusToShow)
    {
        setTooltip (description);
        setHelpText (description);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto* theme = dynamic_cast<Theme*> (&getLookAndFeel());
        const auto accent = theme != nullptr ? theme->getAccent() : Colours::text;

        auto bounds = getLocalBounds().toFloat();
        auto bar = bounds.removeFromBottom (22.0f);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto scope = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre()).reduced (2.0f);
        const auto centre = scope.getCentre();
        const float radius = scope.getWidth() * 0.5f;

        g.setColour (Colours::background.withAlpha (0.7f));
        g.fillEllipse (scope);
        g.setColour (Colours::outline.withAlpha (0.7f));
        g.drawEllipse (scope, 1.0f);

        // Guides: mono (vertical), and the left and right speakers (diagonals).
        const float diagonal = radius * 0.7071f;
        g.setColour (Colours::outline.withAlpha (0.55f));
        g.drawLine (centre.x, scope.getY(), centre.x, scope.getBottom(), 1.0f);
        g.drawLine (centre.x - diagonal, centre.y - diagonal, centre.x + diagonal, centre.y + diagonal, 1.0f);
        g.drawLine (centre.x + diagonal, centre.y - diagonal, centre.x - diagonal, centre.y + diagonal, 1.0f);

        g.setFont (font (10.0f, true));
        g.setColour (Colours::dimText);
        g.drawText ("L", juce::Rectangle<float> (14.0f, 12.0f).withCentre ({ centre.x - diagonal - 6.0f, centre.y - diagonal - 5.0f }), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (14.0f, 12.0f).withCentre ({ centre.x + diagonal + 6.0f, centre.y - diagonal - 5.0f }), juce::Justification::centred);

        if (shownPeak > 0.0f)
        {
            // Scaled to the current level so that quiet tails stay readable.
            const float scale = radius * 0.92f / (juce::jmax (0.02f, shownPeak) * 1.4142f);
            g.setColour (accent.withAlpha (0.55f));

            for (int i = 0; i < numPoints; ++i)
            {
                const float x = centre.x + (right[(size_t) i] - left[(size_t) i]) * scale;
                const float y = centre.y - (right[(size_t) i] + left[(size_t) i]) * scale;
                g.fillRect (x - 0.9f, y - 0.9f, 1.8f, 1.8f);
            }
        }

        // Correlation bar: -1 on the left, +1 on the right.
        bar.removeFromTop (6.0f);
        auto track = bar.removeFromTop (6.0f).reduced (12.0f, 0.0f);
        g.setColour (Colours::control);
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (Colours::outline);
        g.fillRect (track.getCentreX() - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f);

        if (shownPeak > 0.0f)
        {
            const float x = track.getCentreX() + shownCorrelation * track.getWidth() * 0.5f;
            g.setColour (shownCorrelation < 0.0f ? Colours::danger : accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (x, track.getCentreX()), track.getY(),
                                                            std::abs (x - track.getCentreX()), track.getHeight()), 3.0f);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, track.getCentreY() }));
        }

        g.setFont (font (9.5f));
        g.setColour (Colours::dimText);
        g.drawText ("-1", bar.withWidth (24.0f), juce::Justification::centredLeft);
        g.drawText ("+1", bar.withLeft (bar.getRight() - 24.0f), juce::Justification::centredRight);
        g.drawText ("mono match", bar, juce::Justification::centred);
    }

private:
    static constexpr int numPoints = fxtend::EngineStatus::scopeSize;

    static constexpr const char* description =
        "Stereo picture of the output: a thin vertical line is mono, a round cloud is wide. "
        "The bar shows how well left and right agree; to the left of the middle they cancel in mono.";

    void timerCallback() override
    {
        if (! isShowing())
            return;

        float peak = 0.0f;
        double sumLeft = 0.0, sumRight = 0.0, sumBoth = 0.0;

        for (int i = 0; i < numPoints; ++i)
        {
            const float l = status.scopeLeft[(size_t) i].load (std::memory_order_relaxed);
            const float r = status.scopeRight[(size_t) i].load (std::memory_order_relaxed);
            left[(size_t) i] = l;
            right[(size_t) i] = r;
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
            sumLeft += (double) l * l;
            sumRight += (double) r * r;
            sumBoth += (double) l * r;
        }

        const bool wasSilent = shownPeak <= 0.0f;

        if (peak < 1.0e-4f)
        {
            shownPeak = 0.0f;

            if (! wasSilent)
                repaint();

            return;
        }

        const double energy = std::sqrt (sumLeft * sumRight);
        const float correlation = energy > 1.0e-12 ? (float) (sumBoth / energy) : 1.0f;

        shownPeak = juce::jmax (peak, shownPeak * 0.9f);
        shownCorrelation = wasSilent ? correlation : shownCorrelation + 0.3f * (correlation - shownCorrelation);
        repaint();
    }

    const fxtend::EngineStatus& status;
    std::array<float, (size_t) numPoints> left {}, right {};
    float shownPeak = 0.0f, shownCorrelation = 1.0f;
};

} // namespace fxui
