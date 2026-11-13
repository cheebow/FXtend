#pragma once

#include "Theme.h"
#include "../engine/OfflineRenderer.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace fxui
{

/** Overview of the FX as it will sound: the body from 0 to 100 %, the Body End
    marker, and any tail after it. Rendering happens on a background thread. */
class WaveformView final : public juce::Component,
                           private juce::Thread,
                           private juce::AsyncUpdater
{
public:
    WaveformView();
    ~WaveformView() override;

    /** Asks for a new overview. Cheap to call repeatedly; only the latest request is rendered. */
    void setRequest (const fxtend::RenderRequest& request);

    /** Progress of the sounding voice (0..1), or a negative value when silent. */
    void setPlayhead (double phase);

    void setAccent (juce::Colour colour);

    /** Name of a preset that is only being auditioned (empty: the picture is
        the loaded sound). Shown as a badge so the two cannot be mixed up. */
    void setAudition (const juce::String& presetName);

    void paint (juce::Graphics&) override;

private:
    struct Overview
    {
        std::vector<float> minimum, maximum;    // one pair per column
        double bodyFraction = 1.0;              // share of the width taken by the body
        bool valid = false;
    };

    void run() override;
    void handleAsyncUpdate() override;

    static constexpr int numColumns = 720;

    juce::CriticalSection lock;
    fxtend::RenderRequest pendingRequest;
    bool hasPending = false;
    Overview finished;

    Overview shown;
    double playhead = -1.0;
    juce::Colour accent { 0xffffa53d };
    juce::String auditionName;
};

} // namespace fxui
