#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"
#include "../engine/FxTemplates.h"
#include "../preset/FactoryPresets.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace fxui
{

/** The Presets page: every preset at a glance, one column per FX type in that
    type's colour and with its picture, plus a column for the user's own.
    A click on a name loads the preset. The play button at its right only
    plays it, without loading, and stops it again while it sounds. */
class PresetBrowser final : public juce::Component,
                            private juce::Timer
{
public:
    explicit PresetBrowser (FXtendAudioProcessor& processorToUse) : processor (processorToUse), list (*this)
    {
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, Colours::outline);
        addAndMakeVisible (viewport);
        refresh();
        startTimerHz (12);
    }

    /** Called with an error text when a preset could not be loaded. */
    std::function<void (const juce::String&)> onError;

    /** Re-reads the preset list (after saving, renaming, importing...). */
    void refresh()
    {
        auto& manager = processor.getPresetManager();

        for (auto& column : columns)
            column.clear();

        // Presets of another family inside a type ("Fall" among the
        // downlifters, "Whoosh" among the sweeps...) are gathered under a small
        // heading of their own, after the ones named like the column.
        struct Family
        {
            juce::String name;
            std::vector<Item> items;
        };

        std::array<std::vector<Family>, (size_t) fxtend::numFxTypes> families;

        for (int i = 0; i < manager.getNumPresets(); ++i)
        {
            const auto name = manager.getName (i);
            const auto type = manager.getFxType (i);
            const bool own = ! manager.isFactory (i) || name == fxtend::emptyPresetName;

            if (own)
            {
                columns[(size_t) userColumn].push_back ({ i, name, name, type });
                continue;
            }

            // The column already says the type.
            if (name.startsWith (fxtend::getFxTypeName (type)))
            {
                columns[(size_t) type].push_back ({ i, name, name.substring ((int) std::strlen (fxtend::getFxTypeName (type))).trim(), type });
                continue;
            }

            // "Long Clap 02 Wide Wash" -> family "Long Clap", shown "02 Wide Wash".
            const int digit = name.indexOfAnyOf ("0123456789");
            const auto family = digit > 0 ? name.substring (0, digit).trim() : juce::String();

            if (family.isEmpty())
            {
                columns[(size_t) type].push_back ({ i, name, name, type });
                continue;
            }

            auto& list = families[(size_t) type];
            auto found = std::find_if (list.begin(), list.end(), [&family] (const Family& f) { return f.name == family; });

            if (found == list.end())
                found = list.insert (list.end(), Family { family, {} });

            found->items.push_back ({ i, name, name.substring (digit).trim(), type });
        }

        for (size_t type = 0; type < families.size(); ++type)
        {
            for (auto& family : families[type])
            {
                columns[type].push_back ({ -1, {}, family.name, (fxtend::FxType) type });
                columns[type].insert (columns[type].end(), family.items.begin(), family.items.end());
            }
        }

        currentName = processor.getCurrentPresetName();
        resized();
        list.repaint();
    }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds().toFloat());

        // Column headings stay put while the list scrolls.
        for (int column = 0; column < numColumns; ++column)
        {
            auto area = juce::Rectangle<int> (viewport.getX() + column * columnWidth(), 6, columnWidth(), headingHeight).reduced (6, 0);
            const bool isUser = column == userColumn;
            const auto colour = isUser ? Colours::text : fxui::getAccent ((fxtend::FxType) column);

            if (! isUser)
            {
                drawTypeGlyph (g, area.removeFromLeft (24).toFloat().withSizeKeepingCentre (24.0f, 14.0f), (fxtend::FxType) column, colour);
                area.removeFromLeft (7);
            }

            g.setColour (colour);
            g.setFont (font (13.0f, true));
            g.drawText (isUser ? "User" : fxtend::getFxTypeName ((fxtend::FxType) column), area, juce::Justification::centredLeft);

            g.setColour (colour.withAlpha (0.5f));
            g.fillRect (viewport.getX() + column * columnWidth() + 6, 6 + headingHeight, columnWidth() - 12, 2);
        }
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds().reduced (8, 0).withTrimmedTop (6 + headingHeight + 6).withTrimmedBottom (6));

        size_t longest = 1;

        for (const auto& column : columns)
            longest = juce::jmax (longest, column.size());

        list.setSize (viewport.getWidth() - viewport.getScrollBarThickness(), (int) longest * itemHeight + 4);
    }

private:
    static constexpr int numColumns = fxtend::numFxTypes + 1;
    static constexpr int userColumn = fxtend::numFxTypes;
    static constexpr int headingHeight = 24;
    static constexpr int itemHeight = 27;

    struct Item
    {
        int index;                      // -1: a family heading, not a preset
        juce::String name, shown;
        fxtend::FxType type;

        bool isHeading() const noexcept   { return index < 0; }
    };

    /** The scrolling part: the preset names themselves. */
    class List final : public juce::Component
    {
    public:
        explicit List (PresetBrowser& ownerToUse) : owner (ownerToUse) {}

        void paint (juce::Graphics& g) override
        {
            for (int column = 0; column < numColumns; ++column)
            {
                const auto& items = owner.columns[(size_t) column];

                for (int row = 0; row < (int) items.size(); ++row)
                {
                    const auto& item = items[(size_t) row];
                    const auto colour = fxui::getAccent (item.type);

                    if (item.isHeading())
                    {
                        auto heading = cell (column, row).toFloat().reduced (9.0f, 0.0f).withTrimmedTop (7.0f);
                        g.setColour (colour.withAlpha (0.9f));
                        g.setFont (font (11.0f, true));
                        const auto label = item.shown.toUpperCase();
                        const float labelWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 8.0f;
                        g.drawText (label, heading, juce::Justification::centredLeft);
                        g.setColour (colour.withAlpha (0.35f));
                        g.fillRect (heading.withTrimmedLeft (labelWidth).withSizeKeepingCentre (heading.getWidth() - labelWidth, 1.0f).withX (heading.getX() + labelWidth));
                        continue;
                    }

                    const bool current = item.name == owner.currentName;
                    const bool hovered = column == hoverColumn && row == hoverRow;
                    auto area = cell (column, row).toFloat().reduced (3.0f, 2.0f);

                    if (current)
                    {
                        g.setColour (colour);
                        g.fillRoundedRectangle (area, 6.0f);
                    }
                    else
                    {
                        g.setColour (colour.withAlpha (hovered ? 0.28f : 0.10f));
                        g.fillRoundedRectangle (area, 6.0f);
                    }

                    // Play button (a stop square while this preset sounds).
                    {
                        const auto button = playArea (column, row).toFloat();
                        const bool overButton = hovered && hoverPlay;
                        const bool stops = item.index == owner.auditionIndex;
                        const auto mark = button.withSizeKeepingCentre (9.0f, 10.0f);
                        const auto ink = current ? Colours::background : colour;

                        g.setColour (ink.withAlpha (overButton ? 0.30f : 0.14f));
                        g.fillEllipse (button.withSizeKeepingCentre (19.0f, 19.0f));
                        g.setColour (ink.withAlpha (overButton || stops ? 1.0f : 0.8f));

                        if (stops)
                            g.fillRect (mark.withSizeKeepingCentre (8.0f, 8.0f));
                        else
                        {
                            juce::Path triangle;
                            triangle.addTriangle (mark.getX() + 1.5f, mark.getY(), mark.getX() + 1.5f, mark.getBottom(), mark.getRight() + 1.0f, mark.getCentreY());
                            g.fillPath (triangle);
                        }
                    }

                    auto text = area.reduced (8.0f, 0.0f).withTrimmedRight ((float) playWidth - 6.0f);

                    // In the User column the picture tells the type.
                    if (column == userColumn && item.name != fxtend::emptyPresetName)
                    {
                        drawTypeGlyph (g, text.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 11.0f), item.type,
                                       current ? Colours::background : colour);
                        text.removeFromLeft (6.0f);
                    }

                    g.setColour (current ? Colours::background : Colours::text);
                    g.setFont (font (12.5f, current));
                    g.drawFittedText (item.shown, text.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
                }
            }
        }

        void mouseMove (const juce::MouseEvent& event) override   { setHover (event.x, event.y); }
        void mouseExit (const juce::MouseEvent&) override         { setHover (-1, -1); }

        void mouseUp (const juce::MouseEvent& event) override
        {
            int column = 0, row = 0;

            if (event.mouseWasClicked() && find (event.x, event.y, column, row))
            {
                const auto& item = owner.columns[(size_t) column][(size_t) row];

                if (playArea (column, row).contains (event.x, event.y))
                    owner.audition (item);
                else
                    owner.load (item);
            }
        }

    private:
        juce::Rectangle<int> cell (int column, int row) const
        {
            return { column * owner.columnWidth(), row * itemHeight, owner.columnWidth(), itemHeight };
        }

        juce::Rectangle<int> playArea (int column, int row) const
        {
            return cell (column, row).reduced (3, 2).removeFromRight (playWidth);
        }

        bool find (int x, int y, int& column, int& row) const
        {
            if (x < 0 || y < 0)
                return false;

            column = x / juce::jmax (1, owner.columnWidth());
            row = y / itemHeight;
            return column < numColumns && row < (int) owner.columns[(size_t) column].size()
                   && ! owner.columns[(size_t) column][(size_t) row].isHeading();
        }

        void setHover (int x, int y)
        {
            int column = -1, row = -1;

            if (! find (x, y, column, row))
                column = row = -1;

            const bool onPlay = column >= 0 && playArea (column, row).contains (x, y);

            if (column != hoverColumn || row != hoverRow || onPlay != hoverPlay)
            {
                hoverColumn = column;
                hoverRow = row;
                hoverPlay = onPlay;
                setMouseCursor (column >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        PresetBrowser& owner;
        int hoverColumn = -1, hoverRow = -1;
        bool hoverPlay = false;
    };

    int columnWidth() const noexcept   { return juce::jmax (1, (viewport.getWidth() - viewport.getScrollBarThickness()) / numColumns); }

    void load (const Item& item)
    {
        const auto result = processor.loadPreset (item.index);

        if (result.failed() && onError)
            onError (result.getErrorMessage());

        currentName = processor.getCurrentPresetName();
        list.repaint();
    }

    /** The play button: plays the preset without loading it, or stops it
        if it is the one being played. */
    void audition (const Item& item)
    {
        if (item.index == processor.getAuditionIndex())
            processor.stopSound();
        else
        {
            const auto result = processor.auditionPreset (item.index);

            if (result.failed() && onError)
                onError (result.getErrorMessage());
        }

        auditionIndex = processor.getAuditionIndex();
        list.repaint();
    }

    void timerCallback() override
    {
        if (! isShowing())
            return;

        if (const int playing = processor.getAuditionIndex(); playing != auditionIndex)
        {
            auditionIndex = playing;
            list.repaint();
        }

        // Keeps the highlight right when the preset changes elsewhere, and
        // the list right when presets are saved or deleted.
        const auto name = processor.getCurrentPresetName();
        int count = 0;

        for (const auto& column : columns)
            for (const auto& item : column)
                count += item.isHeading() ? 0 : 1;

        if (count != processor.getPresetManager().getNumPresets())
            refresh();
        else if (name != currentName)
        {
            currentName = name;
            list.repaint();
        }
    }

    FXtendAudioProcessor& processor;
    std::array<std::vector<Item>, (size_t) numColumns> columns;
    juce::String currentName;
    int auditionIndex = -1;
    static constexpr int playWidth = 24;
    juce::Viewport viewport;
    List list;
};

} // namespace fxui
