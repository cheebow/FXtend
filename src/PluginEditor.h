#pragma once

#include "PluginProcessor.h"
#include "ui/LayerEditor.h"
#include "ui/LogoView.h"
#include "ui/MeterView.h"
#include "ui/MixerView.h"
#include "ui/PresetBrowser.h"
#include "ui/FxPanel.h"
#include "ui/SamplePanel.h"
#include "ui/StereoScope.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"
#include "engine/WavExport.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** A rotary control with its name above and value below, bound to a host parameter. */
class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& title, bool bipolar);

    void setTitle (const juce::String& title);
    void resized() override;

private:
    juce::Label label;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

class FXtendAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer,
                                          private juce::FocusChangeListener
{
public:
    explicit FXtendAudioProcessorEditor (FXtendAudioProcessor&);
    ~FXtendAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void timerCallback() override;

    // Keeps the keyboard with the host: only a text field being typed into
    // may hold the keyboard focus, so the host's key commands (space bar for
    // play, and so on) keep working after a control has been clicked.
    void globalFocusChanged (juce::Component* focusedComponent) override;

    void rebuildPresetList();
    void setSaveMode (bool shouldShow);
    void commitSave();
    void showMessage (const juce::String& text, bool isError);

    void rebuildLengthChoices();
    void commitLengthText();
    void refreshLengthText();
    void refreshInfo();
    void refreshFxType();
    void requestPreviewIfNeeded();
    void showPage (int page);

    void showFileMenu();
    void showExportMenu();
    void exportWavTo (const juce::File& target);
    void choosePackage (bool isImport);

    FXtendAudioProcessor& audioProcessor;
    fxui::Theme theme;
    juce::TooltipWindow tooltipWindow { this };

    // Header
    fxui::LogoView logo;
    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Save" }, randomButton { "Random" }, undoButton { "Undo" }, fileButton { "File" };
    juce::TextEditor nameEditor;
    juce::TextButton confirmSaveButton { "OK" }, cancelSaveButton { "Cancel" };
    bool saveMode = false;
    bool renameMode = false;        // the name field renames the current preset instead of saving a new one

    // Tempo and length
    juce::Label bpmLabel, timeSigLabel, lengthTitle, lengthInfo, manualBpmTitle;
    juce::Slider manualBpmSlider;
    juce::ComboBox lengthValueBox, lengthUnitBox;
    std::unique_ptr<APVTS::SliderAttachment> manualBpmAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> lengthUnitAttachment;

    fxui::WaveformView waveform;

    // FX type and macros
    std::array<juce::TextButton, fxtend::numFxTypes> typeButtons;
    std::array<std::unique_ptr<ParameterKnob>, fxtend::numMacros> macroKnobs;
    std::unique_ptr<ParameterKnob> gainKnob;
    fxui::MeterView meter;

    fxui::SamplePanel samplePanel;
    fxui::FxPanel fxPanel;
    fxui::LayerEditor layerEditor;
    fxui::MixerView mixerView;
    fxui::PresetBrowser presetBrowser;
    fxui::StereoScope masterScope;      // shown on the Advanced > Master tab

    // The window keeps one size; these switch what the middle of it shows.
    enum Page { presetsPage = 0, mainPage, mixerPage, advancedPage, numPages };
    std::array<juce::TextButton, numPages> pageButtons;
    int currentPage = mainPage;

    // Footer
    juce::TextButton previewButton { "Preview" }, exportButton { "Export WAV" };
    fxtend::WavExportOptions exportOptions;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool exporting = false;
    juce::Label tailTitle, retriggerTitle, messageLabel;
    juce::ComboBox tailBox, retriggerBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> tailAttachment, retriggerAttachment;

    // Change tracking for the timer
    int shownFxType = -1;
    int shownLengthUnit = -1;
    float shownLengthValue = -1.0f;
    int shownPatchRevision = -1;
    juce::String shownInfo;
    fxtend::RenderRequest lastRequest;
    bool hasRequest = false;
    int messageTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXtendAudioProcessorEditor)
};
