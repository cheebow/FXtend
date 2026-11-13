#pragma once

#include "FieldControl.h"
#include "StereoScope.h"
#include "../PluginProcessor.h"

#include <memory>
#include <vector>

namespace fxui
{

/** The Mixer page: one strip per synth layer and sample slot (on/off and
    level) and a master strip (limiter, gain, boost, stereo width), so the
    balance of the whole sound can be set in one place. */
class MixerView final : public juce::Component,
                        private juce::Timer
{
public:
    explicit MixerView (FXtendAudioProcessor& processorToUse) : processor (processorToUse)
    {
        using namespace fxtend;
        auto& state = processor.getParameters();

        for (int i = 0; i < numSynthLayers; ++i)
            addStrip (state, find (getSynthLayerFields(), "on"), find (getSynthLayerFields(), "level"),
                      [i] (const auto& field) { return ParamIDs::synthLayer (i, field); });

        for (int i = 0; i < numSampleLayers; ++i)
            addStrip (state, find (getSampleLayerFields(), "on"), find (getSampleLayerFields(), "level"),
                      [i] (const auto& field) { return ParamIDs::sampleLayer (i, field); });

        // Master: limiter on/off, output gain, limiter boost and stereo width.
        auto& master = strips.emplace_back();
        const auto* limiter = find (getMasterFields(), "limiter");
        const auto* boost = find (getMasterFields(), "limiterBoost");
        master.top = std::make_unique<FieldControl> (state, FieldControl::describe (*limiter, ParamIDs::master (*limiter)));
        auto boostDescription = FieldControl::describe (*boost, ParamIDs::master (*boost));
        boostDescription.name = "Boost";
        master.bottom = std::make_unique<FieldControl> (state, boostDescription);
        const auto* width = find (getMasterFields(), "width");
        master.extra = std::make_unique<FieldControl> (state, FieldControl::describe (*width, ParamIDs::master (*width)));
        setUpFader (master, state, ParamIDs::masterGain);

        addAndMakeVisible (scope);

        for (auto& strip : strips)
        {
            addAndMakeVisible (*strip.top);
            addAndMakeVisible (*strip.fader);

            for (auto* control : { strip.bottom.get(), strip.extra.get() })
                if (control != nullptr)
                    addAndMakeVisible (*control);
        }

        refresh();
        startTimerHz (8);
    }

    /** Where the output meter goes (beside the master fader), in this component's coordinates. */
    juce::Rectangle<int> getMeterArea() const noexcept { return meterArea; }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds().toFloat());

        static const char* const groups[] = { "SYNTH", "SAMPLES", "MASTER" };
        const int starts[] = { 0, fxtend::numSynthLayers, fxtend::numSynthLayers + fxtend::numSampleLayers };

        for (int group = 0; group < 3; ++group)
        {
            const int x = stripX (starts[group]);
            g.setFont (font (10.5f, true));
            g.setColour (Colours::dimText);
            g.drawText (groups[group], x + 6, 6, 120, 14, juce::Justification::centredLeft);

            if (group > 0)
            {
                g.setColour (Colours::outline.withAlpha (0.45f));
                g.fillRect (x - 4, 10, 1, getHeight() - 20);
            }
        }

        for (size_t i = 0; i < strips.size(); ++i)
        {
            g.setFont (font (12.5f, true));
            g.setColour (strips[i].active ? Colours::text : Colours::dimText.withAlpha (0.6f));
            g.drawFittedText (strips[i].title, stripX ((int) i) + 2, 24, (i + 1 == strips.size() ? masterFaderWidth : stripWidth()) - 4, 18,
                              juce::Justification::centred, 1, 0.75f);
        }
    }

    void resized() override
    {
        for (size_t i = 0; i < strips.size(); ++i)
        {
            const bool isMaster = i + 1 == strips.size();
            auto area = juce::Rectangle<int> (stripX ((int) i), 44, isMaster ? getWidth() - 8 - stripX ((int) i) : stripWidth(), getHeight() - 50).reduced (5, 0);

            if (isMaster)
            {
                // Right of the fader: the stereo display with Width and Boost under it.
                auto side = area.removeFromRight (area.getWidth() - masterFaderWidth + 5);
                strips[i].bottom->setBounds (side.removeFromBottom (fieldRowHeight));
                strips[i].extra->setBounds (side.removeFromBottom (fieldRowHeight));
                scope.setBounds (side.withTrimmedTop (-18).reduced (0, 2));
            }

            strips[i].top->setBounds (area.removeFromTop (fieldRowHeight - 10).translated (isMaster ? 0 : 14, -12));
            area.reduce (0, 4);

            if (isMaster)
            {
                meterArea = area.removeFromRight (22).reduced (0, 4).withTrimmedBottom (22);
                area.removeFromRight (2);
            }

            strips[i].fader->setBounds (area);
        }
    }

private:
    struct Strip
    {
        std::unique_ptr<FieldControl> top, bottom, extra;       // bottom and extra: master strip only
        std::unique_ptr<juce::Slider> fader;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String title;
        bool active = true;
    };

    template <typename FieldType>
    static const FieldType* find (std::span<const FieldType> fields, const char* id)
    {
        for (const auto& field : fields)
            if (juce::String (id) == field.id)
                return &field;

        jassertfalse;
        return nullptr;
    }

    template <typename FieldType, typename IdOf>
    void addStrip (juce::AudioProcessorValueTreeState& state, const FieldType* on, const FieldType* level, IdOf&& idOf)
    {
        auto& strip = strips.emplace_back();
        strip.top = std::make_unique<FieldControl> (state, FieldControl::describe (*on, idOf (*on)));
        setUpFader (strip, state, idOf (*level));
    }

    static void setUpFader (Strip& strip, juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
    {
        strip.fader = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
        strip.fader->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 20);
        strip.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, *strip.fader);
        strip.fader->setTextValueSuffix (" dB");
    }

    static constexpr int masterFaderWidth = 96;
    static constexpr int masterWidth = 236;     // fader and meter, then the stereo display

    int stripWidth() const noexcept  { return (getWidth() - 16 - masterWidth) / (int) juce::jmax ((size_t) 1, strips.size() - 1); }
    int stripX (int index) const noexcept { return 8 + index * stripWidth(); }

    void timerCallback() override
    {
        if (isShowing())
            refresh();
    }

    void refresh()
    {
        using namespace fxtend;
        static const char* const typeNames[] = { "Noise", "Sub", "FM", "Transient", "Clap" };
        const auto patch = processor.getCurrentPatch();
        bool changed = false;

        const auto set = [&changed] (Strip& strip, const juce::String& title, bool active)
        {
            if (strip.title != title || strip.active != active)
            {
                strip.title = title;
                strip.active = active;
                strip.fader->setAlpha (active ? 1.0f : 0.4f);
                changed = true;
            }
        };

        for (int i = 0; i < numSynthLayers; ++i)
        {
            const auto& layer = patch.synth[(size_t) i];
            set (strips[(size_t) i], juce::String (i + 1) + "  " + typeNames[juce::jlimit (0, 4, (int) layer.getType())], layer.isEnabled());
        }

        for (int i = 0; i < numSampleLayers; ++i)
        {
            const auto info = processor.getSampleInfo (i);
            auto& strip = strips[(size_t) (numSynthLayers + i)];
            const bool filled = ! info.ref.isEmpty();
            set (strip, filled ? info.ref.name : "Sample " + juce::String (i + 1), filled && patch.samples[(size_t) i].isEnabled());
            strip.top->setVisible (filled);
        }

        set (strips.back(), "Output", true);

        if (changed)
            repaint();
    }

    FXtendAudioProcessor& processor;
    std::vector<Strip> strips;
    StereoScope scope { processor.getEngine().getStatus() };
    juce::Rectangle<int> meterArea;
};

} // namespace fxui
