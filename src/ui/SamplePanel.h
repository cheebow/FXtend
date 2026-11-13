#pragma once

#include "FieldControl.h"
#include "../PluginProcessor.h"

#include <functional>
#include <memory>
#include <vector>

namespace fxui
{

/** The sample row of the main view: four slots to load WAV/AIFF files into
    (by button or drag and drop), the most used settings of the selected slot,
    and what state its sample is in. */
class SamplePanel final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    explicit SamplePanel (FXtendAudioProcessor& processor);
    ~SamplePanel() override;

    static int getPreferredHeight() noexcept;

    int getSelectedSlot() const noexcept { return selectedSlot; }

    /** Called when the user picks another slot. */
    std::function<void (int)> onSlotSelected;

    /** Called with something to tell the user (errors from loading, for example). */
    std::function<void (const juce::String&, bool isError)> onMessage;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    /** A slot is filled in when it holds a sample and outlined when it is the
        selected one, so "has a sample" and "is selected" never look alike.
        A right click (or ctrl-click) opens the slot's menu instead of clicking. */
    class SlotButton final : public juce::TextButton
    {
    public:
        void setState (bool holdsSample, bool isSelected);
        std::function<void()> onMenu;

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        bool holdsSample = false, selected = false;
    };

    void timerCallback() override;
    void selectSlot (int slot);
    void showSlotMenu (int slot);
    void rebuildControls();
    void refresh();
    void rebuildThumbnail();
    void chooseFile (int slot);
    void loadFiles (const juce::StringArray& files, int firstSlot);
    int slotAt (int x, int y) const;

    FXtendAudioProcessor& processor;
    int selectedSlot = 0;
    int dragTargetSlot = -1;
    int shownRevision = -1;
    float shownTrimStart = -1.0f, shownTrimEnd = -1.0f;
    bool shownReverse = false;

    std::array<SlotButton, fxtend::numSampleLayers> slotButtons;
    std::array<juce::ToggleButton, fxtend::numSampleLayers> slotToggles;      // per-slot on/off
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, fxtend::numSampleLayers> toggleAttachments;
    juce::TextButton loadButton { "Load..." }, clearButton { "Clear" };
    juce::Label statusLabel;
    std::vector<std::unique_ptr<FieldControl>> controls;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> thumbnailArea;
    std::vector<float> thumbnail;       // peak per column, 0..1
};

} // namespace fxui
