#pragma once

#include "FieldControl.h"
#include "Theme.h"
#include "../Parameters.h"

#include <memory>
#include <vector>

namespace fxui
{

/** The effects row of the main view: which reverb and delay and how much of
    each, then how hard the sound is squeezed (compressor) and pushed (limiter). */
class FxPanel final : public juce::Component
{
public:
    explicit FxPanel (juce::AudioProcessorValueTreeState& state)
    {
        // Only what and how much; the fine settings are under Advanced > Master.
        static constexpr const char* ids[] = { "reverbType", "reverbMix", "delayType", "delayMix", "compAmount", "limiterBoost", "width" };

        for (const char* id : ids)
        {
            for (const auto& field : fxtend::getMasterFields())
            {
                if (juce::String (id) == field.id)
                {
                    auto description = FieldControl::describe (field, ParamIDs::master (field));

                    if (description.id == "width")
                        description.name = "Stereo Width";

                    controls.push_back (std::make_unique<FieldControl> (state, description));
                    addAndMakeVisible (*controls.back());
                }
            }
        }
    }

    static int getPreferredHeight() noexcept { return 4 + fieldRowHeight + 4; }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds().toFloat());
        g.setFont (font (10.5f, true));
        g.setColour (Colours::dimText);
        g.drawText ("FX", 10, 4, fieldTitleWidth - 10, fieldRowHeight, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto row = getLocalBounds().reduced (6, 4);
        row.removeFromLeft (fieldTitleWidth - 6);
        const int groupGap = 14;                        // between reverb, delay, dynamics and stereo
        const int cellWidth = (row.getWidth() - 3 * groupGap) / (int) juce::jmax ((size_t) 1, controls.size());

        for (size_t i = 0; i < controls.size(); ++i)
        {
            if (i == 2 || i == 4 || i == 6)
                row.removeFromLeft (groupGap);

            controls[i]->setBounds (row.removeFromLeft (cellWidth));
        }
    }

private:
    std::vector<std::unique_ptr<FieldControl>> controls;
};

} // namespace fxui
