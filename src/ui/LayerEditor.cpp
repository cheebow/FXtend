#include "LayerEditor.h"

using namespace fxtend;

namespace fxui
{

namespace
{
    constexpr int tabHeight = 28;
    constexpr int numRows = 7;
    constexpr int firstSampleTab = numSynthLayers;
    constexpr int masterTab = numSynthLayers + numSampleLayers;

    constexpr bool isSampleTab (int tab) noexcept { return tab >= firstSampleTab && tab < masterTab; }

    constexpr int maxPerRow = 8;

    enum Graph { noGraph = 0, spanGraph, volumeGraph, pitchGraph, filterGraph };

    /** A titled row of settings. Settings that do nothing for the layer's type
        are hidden and the rest close up, so a row may list more than fit at once. */
    struct Row
    {
        const char* title;
        std::array<const char*, maxPerRow> ids;
        int graph;
    };

    constexpr Row synthRows[numRows] =
    {
        { "LAYER",  { "on", "type", "level", "pan", "width", "drive", "", "" }, noGraph },
        { "SOURCE", { "variant", "baseHz", "fmRatio", "fmIndexStart", "fmIndexEnd", "count", "spread", "decay" }, noGraph },
        { "TIME",   { "start", "end", "attack", "ampDecay", "", "", "", "" }, spanGraph },
        { "VOLUME", { "ampStart", "ampEnd", "ampShape", "ampTension", "", "", "", "" }, volumeGraph },
        { "PITCH",  { "pitchStart", "pitchEnd", "pitchShape", "pitchTension", "pitchDrop", "pitchDropMs", "", "" }, pitchGraph },
        { "FILTER", { "filter", "cutoffStart", "cutoffEnd", "cutoffShape", "cutoffTension", "resonance", "", "" }, filterGraph },
        { "LFO",    { "lfoShape", "lfoRateStart", "lfoRateEnd", "lfoPitch", "lfoCutoff", "lfoAmp", "", "" }, noGraph },
    };

    constexpr Row sampleRows[numRows] =
    {
        { "PLAY",   { "on", "mode", "level", "pan", "width", "pitch", "reverse", "" }, noGraph },
        { "TIME",   { "start", "end", "trimStart", "trimEnd", "fadeIn", "fadeOut", "", "" }, spanGraph },
        { "VOLUME", { "ampStart", "ampEnd", "ampShape", "ampTension", "", "", "", "" }, volumeGraph },
        {}, {}, {}, {},
    };

    constexpr Row masterRows[numRows] =
    {
        { "TONE",   { "highPass", "lowPass", "tilt", "drive", "", "", "", "" }, noGraph },
        { "DELAY",  { "delayType", "delayMix", "delayTime", "delayFeedback", "", "", "", "" }, noGraph },
        { "REVERB", { "reverbType", "reverbMix", "reverbSize", "reverbDamping", "", "", "", "" }, noGraph },
        { "COMP",   { "compAmount", "compAttack", "compRelease", "", "", "", "", "" }, noGraph },
        { "LIMIT",  { "limiter", "limiterBoost", "", "", "", "", "", "" }, noGraph },
        { "STEREO", { "width", "bassMono", "", "", "", "", "", "" }, noGraph },
        { "IMAGER", { "widthLow", "widthMid", "widthHigh", "imagerLowHz", "imagerHighHz", "", "", "" }, noGraph },
    };

    const Row* rowsFor (int tab) noexcept
    {
        return tab == masterTab ? masterRows : (isSampleTab (tab) ? sampleRows : synthRows);
    }

    /** Inside a titled row the title already says what the settings belong to. */
    juce::String shortName (const juce::String& id, const juce::String& fullName)
    {
        static const std::pair<const char*, const char*> names[] =
        {
            { "ampStart", "From" },      { "ampEnd", "To" },        { "ampShape", "Curve" },     { "ampTension", "Bend" },
            { "pitchStart", "From" },    { "pitchEnd", "To" },      { "pitchShape", "Curve" },   { "pitchTension", "Bend" },
            { "cutoffStart", "From" },   { "cutoffEnd", "To" },     { "cutoffShape", "Curve" },  { "cutoffTension", "Bend" },
            { "filter", "Type" },        { "ampDecay", "Decay" },
            { "lfoShape", "Shape" },     { "lfoRateStart", "Rate From" }, { "lfoRateEnd", "Rate To" },
            { "compAmount", "Amount" },  { "compAttack", "Attack" },      { "compRelease", "Release" },
            { "limiterBoost", "Boost" }, { "limiter", "On" },
            { "widthLow", "Low" },       { "widthMid", "Mid" },           { "widthHigh", "High" },
            { "imagerLowHz", "Low / Mid" }, { "imagerHighHz", "Mid / High" },
            { "lfoPitch", "> Pitch" },   { "lfoCutoff", "> Filter" },     { "lfoAmp", "> Volume" },
        };

        for (const auto& [key, name] : names)
            if (id == key)
                return name;

        return fullName;
    }

    const char* const rowHelp[numRows] =
    {
        "What this layer is and how loud it is",
        "Settings of the sound source itself",
        "Where in the body the layer plays (0 = start, 1 = end)",
        "Volume from the layer's start to its end",
        "Pitch in semitones from start to end, plus a quick drop at the very start (Punch)",
        "Filter cutoff from start to end",
        "Wobble: how fast, and how much it moves pitch, filter and volume",
    };

    template <typename FieldType>
    const FieldType* find (std::span<const FieldType> fields, const juce::String& id)
    {
        for (const auto& field : fields)
            if (id == field.id)
                return &field;

        return nullptr;
    }

    juce::StringArray variantNames (SynthType type)
    {
        switch (type)
        {
            case SynthType::noise:      return { "White", "Pink", "Brown" };
            case SynthType::sub:        return { "Sine", "Triangle" };
            case SynthType::transient:  return { "Noise", "Click" };
            case SynthType::fm:
            case SynthType::clap:       break;
        }

        return {};
    }

    bool isRelevant (const juce::String& id, SynthType type)
    {
        const bool burst = type == SynthType::transient || type == SynthType::clap;

        if (id == "count" || id == "spread" || id == "decay")
            return burst;

        if (id == "fmRatio" || id == "fmIndexStart" || id == "fmIndexEnd")
            return type == SynthType::fm;

        if (id == "baseHz")
            return type == SynthType::sub || type == SynthType::fm || type == SynthType::transient;

        if (id == "variant")
            return ! variantNames (type).isEmpty();

        if (id == "width")
            return type != SynthType::sub;

        return true;
    }
}

LayerEditor::LayerEditor (juce::AudioProcessorValueTreeState& stateToUse, std::function<fxtend::Patch()> patchSourceToUse)
    : state (stateToUse), patchSource (std::move (patchSourceToUse))
{
    if (patchSource)
        shownPatch = patchSource();

    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        auto& tab = tabs[(size_t) i];
        tab.onClick = [this, i] { selectTab (i); };
        addAndMakeVisible (tab);
    }

    tabs[(size_t) masterTab].setButtonText ("Master");

    rebuild();
    refreshTabs();
    startTimerHz (10);
}

LayerEditor::~LayerEditor() = default;

int LayerEditor::getPreferredHeight() noexcept
{
    return tabHeight + 8 + numRows * fieldRowHeight + 8;
}

void LayerEditor::setSampleSlot (int slot)
{
    // Follows the slot picked on the Main page, but only while a sample tab is open.
    if (isSampleTab (selectedTab))
        selectTab (firstSampleTab + juce::jlimit (0, numSampleLayers - 1, slot));
}

void LayerEditor::selectTab (int tab)
{
    if (tab != selectedTab)
    {
        selectedTab = tab;

        if (isSampleTab (tab))
            sampleSlot = tab - firstSampleTab;

        rebuild();
        refreshTabs();
    }
}

void LayerEditor::rebuild()
{
    controls.clear();
    const auto* rows = rowsFor (selectedTab);

    for (int row = 0; row < numRows; ++row)
    {
        for (const char* id : rows[row].ids)
        {
            if (id == nullptr || *id == 0)
                continue;

            FieldControl::Description description;

            if (selectedTab == masterTab)
            {
                if (const auto* field = find (getMasterFields(), id))
                    description = FieldControl::describe (*field, ParamIDs::master (*field));
            }
            else if (isSampleTab (selectedTab))
            {
                if (const auto* field = find (getSampleLayerFields(), id))
                    description = FieldControl::describe (*field, ParamIDs::sampleLayer (sampleSlot, *field));
            }
            else if (const auto* field = find (getSynthLayerFields(), id))
            {
                description = FieldControl::describe (*field, ParamIDs::synthLayer (selectedTab, *field));
                description.hasDynamicChoices = description.id == "variant";
            }

            if (description.parameterId.isNotEmpty())
            {
                description.name = shortName (description.id, description.name);
                controls.push_back (std::make_unique<FieldControl> (state, description));
                addAndMakeVisible (*controls.back());
            }
        }
    }

    shownType = -1;
    refreshRelevance();
    layoutControls();
    repaint();
}

void LayerEditor::refreshTabs()
{
    static const char* const typeNames[] = { "Noise", "Sub", "FM", "Transient", "Clap" };
    const auto* onField = find (getSynthLayerFields(), "on");
    const auto* typeField = find (getSynthLayerFields(), "type");

    for (int i = 0; i < numSynthLayers; ++i)
    {
        const bool on = state.getRawParameterValue (ParamIDs::synthLayer (i, *onField))->load() >= 0.5f;
        const int type = juce::jlimit (0, 4, juce::roundToInt (state.getRawParameterValue (ParamIDs::synthLayer (i, *typeField))->load()));

        auto& tab = tabs[(size_t) i];
        const auto text = juce::String (i + 1) + "  " + typeNames[type] + (on ? "" : "  (off)");

        if (tab.getButtonText() != text)
            tab.setButtonText (text);

        tab.setAlpha (on || i == selectedTab ? 1.0f : 0.55f);
    }

    for (int i = 0; i < numSampleLayers; ++i)
    {
        const auto text = "Sample " + juce::String (i + 1);

        if (tabs[(size_t) (firstSampleTab + i)].getButtonText() != text)
            tabs[(size_t) (firstSampleTab + i)].setButtonText (text);
    }

    for (int i = 0; i < (int) tabs.size(); ++i)
        tabs[(size_t) i].setToggleState (i == selectedTab, juce::dontSendNotification);
}

void LayerEditor::refreshRelevance()
{
    if (selectedTab >= numSynthLayers)
        return;

    const auto* typeField = find (getSynthLayerFields(), "type");
    const int type = juce::jlimit (0, 4, juce::roundToInt (state.getRawParameterValue (ParamIDs::synthLayer (selectedTab, *typeField))->load()));

    if (type == shownType)
        return;

    shownType = type;
    const auto synthType = (SynthType) type;

    for (auto& control : controls)
    {
        if (control->field.hasDynamicChoices)
            control->setChoices (variantNames (synthType),
                                 synthType == SynthType::noise ? "Color" : (synthType == SynthType::sub ? "Wave" : "Source"));

        control->setVisible (isRelevant (control->field.id, synthType));
    }

    layoutControls();
}

void LayerEditor::timerCallback()
{
    refreshTabs();
    refreshRelevance();

    if (patchSource && isShowing())
    {
        const auto patch = patchSource();
        const auto differs = [] (const auto& one, const auto& other) { return std::memcmp (&one, &other, sizeof (one)) != 0; };

        if (differs (patch.synth, shownPatch.synth) || differs (patch.samples, shownPatch.samples))
        {
            shownPatch = patch;
            repaint();
        }
    }
}

void LayerEditor::paintGraph (juce::Graphics& g, juce::Rectangle<float> area, int kind) const
{
    auto* theme = dynamic_cast<Theme*> (&getLookAndFeel());
    const auto accent = theme != nullptr ? theme->getAccent() : Colours::text;

    g.setColour (Colours::control.withAlpha (0.6f));
    g.fillRoundedRectangle (area, 3.0f);

    const bool isSample = isSampleTab (selectedTab);
    const auto& synth = shownPatch.synth[(size_t) juce::jlimit (0, numSynthLayers - 1, selectedTab)];
    const auto& sample = shownPatch.samples[(size_t) juce::jlimit (0, numSampleLayers - 1, sampleSlot)];

    const float start = juce::jlimit (0.0f, 1.0f, isSample ? sample.start : synth.start);
    const float end = juce::jlimit (start, 1.0f, isSample ? sample.end : synth.end);
    const auto plot = area.reduced (3.0f, 4.0f);
    const float left = plot.getX() + start * plot.getWidth();
    const float right = juce::jmax (left + 2.0f, plot.getX() + end * plot.getWidth());

    if (kind == spanGraph)
    {
        // Where in the body the layer plays.
        g.setColour (accent);
        g.fillRoundedRectangle (left, plot.getCentreY() - 4.0f, right - left, 8.0f, 2.0f);
        return;
    }

    if (kind == filterGraph && synth.getFilterType() == FilterType::off)
    {
        g.setColour (Colours::dimText);
        g.setFont (font (10.5f));
        g.drawText ("filter off", area, juce::Justification::centred);
        return;
    }

    // Height of the curve (0..1) at progress x through the layer.
    const auto valueAt = [&] (float x)
    {
        if (kind == volumeGraph)
            return juce::jlimit (0.0f, 1.0f, isSample ? sample.ampAt (x) : synth.ampAt (x));

        const float curve = kind == pitchGraph
                                ? evaluateCurve ((CurveShape) (int) (synth.pitchShape + 0.5f), synth.pitchTension, synth.pitchPoints, x)
                                : synth.filterCurveAt (x);
        const float from = kind == pitchGraph ? synth.pitchStart : std::log (juce::jmax (1.0f, synth.cutoffStart));
        const float to = kind == pitchGraph ? synth.pitchEnd : std::log (juce::jmax (1.0f, synth.cutoffEnd));

        if (juce::approximatelyEqual (from, to))
            return 0.5f;

        return from < to ? curve : 1.0f - curve;
    };

    juce::Path line;
    const int steps = 40;

    for (int i = 0; i <= steps; ++i)
    {
        const float x = (float) i / (float) steps;
        const float px = left + x * (right - left);
        const float py = plot.getBottom() - valueAt (x) * plot.getHeight();

        if (i == 0)
            line.startNewSubPath (px, py);
        else
            line.lineTo (px, py);
    }

    auto fill = line;
    fill.lineTo (right, plot.getBottom());
    fill.lineTo (left, plot.getBottom());
    fill.closeSubPath();

    g.setColour (accent.withAlpha (0.25f));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (line, juce::PathStrokeType (1.5f));
}

void LayerEditor::paint (juce::Graphics& g)
{
    paintPanel (g, getLocalBounds().toFloat());

    const auto* rows = rowsFor (selectedTab);
    const int cellWidth = gridArea.getWidth() / fieldColumns;

    for (int row = 0; row < numRows; ++row)
    {
        if (rows[row].title == nullptr)
            continue;

        const int y = gridArea.getY() + row * fieldRowHeight;

        if (row > 0)
        {
            g.setColour (Colours::outline.withAlpha (0.35f));
            g.fillRect (10, y, getWidth() - 20, 1);
        }

        g.setFont (font (10.5f, true));
        g.setColour (Colours::dimText);
        g.drawText (rows[row].title, 10, y, fieldTitleWidth - 10, fieldRowHeight, juce::Justification::centredLeft);

        if (rows[row].graph != noGraph)
            paintGraph (g, juce::Rectangle<int> (gridArea.getX() + (fieldColumns - 1) * cellWidth, y, cellWidth, fieldRowHeight)
                               .reduced (4, 6).toFloat(), rows[row].graph);
    }
}

void LayerEditor::resized()
{
    auto bounds = getLocalBounds().reduced (6, 4);
    auto tabArea = bounds.removeFromTop (tabHeight);
    const int tabWidth = tabArea.getWidth() / (int) tabs.size();

    for (auto& tab : tabs)
        tab.setBounds (tabArea.removeFromLeft (tabWidth).reduced (2, 0));

    bounds.removeFromTop (4);
    bounds.removeFromLeft (fieldTitleWidth - 6);
    gridArea = bounds;
    layoutControls();
}

void LayerEditor::setMasterSideComponent (juce::Component* component)
{
    masterSide = component;

    if (masterSide != nullptr)
        addChildComponent (*masterSide);

    layoutControls();
}

void LayerEditor::layoutControls()
{
    if (masterSide != nullptr)
    {
        // The two right-hand columns of the Master tab are free in every row.
        const int cell = gridArea.getWidth() / fieldColumns;
        masterSide->setVisible (selectedTab == masterTab);
        masterSide->setBounds (juce::Rectangle<int> (gridArea.getX() + 5 * cell, gridArea.getY(), 2 * cell, (numRows - 1) * fieldRowHeight)
                                   .reduced (10, 8));
    }

    const int cellWidth = gridArea.getWidth() / fieldColumns;
    const auto* rows = rowsFor (selectedTab);

    for (int row = 0; row < numRows; ++row)
    {
        int column = 0;

        for (const char* id : rows[row].ids)
        {
            if (id == nullptr || *id == 0)
                continue;

            for (auto& control : controls)
            {
                if (control->field.id == id && control->isVisible())
                {
                    control->setBounds (gridArea.getX() + column * cellWidth, gridArea.getY() + row * fieldRowHeight, cellWidth, fieldRowHeight);
                    ++column;
                }
            }
        }
    }
}

juce::String LayerEditor::getTooltip()
{
    if (selectedTab >= numSynthLayers)
        return {};

    const auto position = getMouseXYRelative();

    if (position.x < gridArea.getX() && position.y >= gridArea.getY())
        return rowHelp[juce::jlimit (0, numRows - 1, (position.y - gridArea.getY()) / fieldRowHeight)];

    return {};
}

} // namespace fxui
