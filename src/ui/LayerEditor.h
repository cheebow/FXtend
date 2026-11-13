#pragma once

#include "FieldControl.h"
#include "../engine/Patch.h"

#include <functional>
#include <memory>
#include <vector>

namespace fxui
{

/** The "Advanced" page: a tab per synth layer, one per sample slot and one
    for Master.

    Only the settings that do something for the layer's current type are shown,
    and the rows that describe a change over time (volume, pitch, filter) end
    in a small graph of that change across the body. */
class LayerEditor final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Timer
{
public:
    LayerEditor (juce::AudioProcessorValueTreeState& state, std::function<fxtend::Patch()> patchSource);
    ~LayerEditor() override;

    /** Which sample slot the Sample tab edits (chosen in the sample panel). */
    void setSampleSlot (int slot);

    /** Something to show beside the settings of the Master tab (the stereo display). Not owned. */
    void setMasterSideComponent (juce::Component* component);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::String getTooltip() override;

    static int getPreferredHeight() noexcept;

private:
    void timerCallback() override;
    void selectTab (int tab);
    void rebuild();
    void refreshTabs();
    void refreshRelevance();
    void layoutControls();
    void paintGraph (juce::Graphics&, juce::Rectangle<float> area, int kind) const;

    juce::AudioProcessorValueTreeState& state;
    int selectedTab = 0;
    int sampleSlot = 0;
    int shownType = -1;
    std::function<fxtend::Patch()> patchSource;
    fxtend::Patch shownPatch;
    juce::Rectangle<int> gridArea;
    juce::Component* masterSide = nullptr;

    // Four synth layers, four sample slots, Master.
    std::array<juce::TextButton, fxtend::numSynthLayers + fxtend::numSampleLayers + 1> tabs;
    std::vector<std::unique_ptr<FieldControl>> controls;
};

} // namespace fxui
