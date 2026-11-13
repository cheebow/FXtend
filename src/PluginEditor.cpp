#include "PluginEditor.h"
#include "engine/FxTemplates.h"
#include "preset/FactoryPresets.h"
#include "preset/PresetPackage.h"

#include <cstring>

using namespace fxtend;
using fxui::Colours::dimText;

namespace
{
    constexpr int editorWidth = 940;
    constexpr int margin = 12;
    constexpr int headerHeight = 40;
    constexpr int tempoHeight = 34;
    constexpr int waveformHeight = 150;
    constexpr int compactWaveformHeight = 78;     // on the Mixer and Advanced pages
    constexpr int typeHeight = 32;
    constexpr int macroHeight = 104;
    constexpr int footerHeight = 34;
    constexpr int gap = 8;

    // One size for every page: header, tempo row and footer stay, the middle changes.
    int editorHeight()
    {
        return margin * 2 + headerHeight + typeHeight + tempoHeight + footerHeight + gap * 5
               + compactWaveformHeight + fxui::LayerEditor::getPreferredHeight();
    }

    void styleCaption (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (fxui::font (11.0f, true));
        label.setColour (juce::Label::textColourId, dimText);
        label.setJustificationType (juce::Justification::centredRight);
    }

    juce::String formatNumber (double value)
    {
        if (std::abs (value - 0.5) < 1.0e-6)
            return "1/2";

        if (std::abs (value - std::round (value)) < 1.0e-6)
            return juce::String ((int) std::round (value));

        return juce::String (value, 2);
    }

    /** Accepts "4", "0.5" and "1/2". Returns a negative number if it makes no sense. */
    double parseLength (const juce::String& text)
    {
        const auto trimmed = text.trim();

        if (trimmed.containsChar ('/'))
        {
            const double numerator = trimmed.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
            const double denominator = trimmed.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
            return denominator > 0.0 ? numerator / denominator : -1.0;
        }

        return trimmed.containsOnly ("0123456789.") && trimmed.isNotEmpty() ? trimmed.getDoubleValue() : -1.0;
    }
}

//==============================================================================

ParameterKnob::ParameterKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& title, bool bipolar)
    : attachment (state, parameterId, slider)
{
    label.setFont (fxui::font (12.0f, true));
    label.setJustificationType (juce::Justification::centred);
    label.setText (title, juce::dontSendNotification);
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);
}

void ParameterKnob::setTitle (const juce::String& title)
{
    label.setText (title, juce::dontSendNotification);
}

void ParameterKnob::resized()
{
    auto bounds = getLocalBounds();
    label.setBounds (bounds.removeFromTop (18));
    slider.setBounds (bounds);
}

//==============================================================================

FXtendAudioProcessorEditor::FXtendAudioProcessorEditor (FXtendAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), samplePanel (p), fxPanel (p.getParameters()), layerEditor (p.getParameters(), [&p] { return p.getCurrentPatch(); }), mixerView (p), presetBrowser (p), masterScope (p.getEngine().getStatus())
{
    setLookAndFeel (&theme);
    auto& state = audioProcessor.getParameters();

    // ---- Header
    addAndMakeVisible (logo);

    static const char* const pageNames[] = { "Presets", "Main", "Mixer", "Advanced" };
    static const char* const pageTips[] = { "Every preset at a glance - click one to load it",
                                            "Type, macros, samples and effects",
                                            "Level and pan of every layer in one place",
                                            "Every setting of each layer, sample and the master" };

    for (int i = 0; i < numPages; ++i)
    {
        auto& button = pageButtons[(size_t) i];
        button.setButtonText (pageNames[i]);
        button.setTooltip (pageTips[i]);
        button.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < numPages - 1 ? juce::Button::ConnectedOnRight : 0));
        button.onClick = [this, i] { showPage (i); };
        addAndMakeVisible (button);
    }

    presetBox.setTextWhenNothingSelected ("Preset");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;

        if (index >= 0)
        {
            const auto result = audioProcessor.loadPreset (index);

            if (result.failed())
                showMessage (result.getErrorMessage(), true);
        }
    };
    addAndMakeVisible (presetBox);

    saveButton.onClick = [this] { setSaveMode (true); };
    randomButton.onClick = [this] { audioProcessor.randomise(); };
    undoButton.onClick = [this] { audioProcessor.undoRandomise(); };
    confirmSaveButton.onClick = [this] { commitSave(); };
    cancelSaveButton.onClick = [this] { setSaveMode (false); };
    nameEditor.onReturnKey = [this] { commitSave(); };
    nameEditor.onEscapeKey = [this] { setSaveMode (false); };
    nameEditor.setTextToShowWhenEmpty ("Preset name", dimText);
    randomButton.setTooltip ("Changes the macro knobs, keeping the FX type");

    fileButton.onClick = [this] { showFileMenu(); };
    fileButton.setTooltip ("Preset packages: a preset together with its samples, for moving to another computer");

    for (auto* component : std::initializer_list<juce::Component*> { &saveButton, &randomButton, &undoButton, &fileButton })
        addAndMakeVisible (component);

    for (auto* component : std::initializer_list<juce::Component*> { &nameEditor, &confirmSaveButton, &cancelSaveButton })
        addChildComponent (component);

    // ---- Tempo and length
    bpmLabel.setFont (fxui::font (14.0f, true));
    timeSigLabel.setFont (fxui::font (14.0f, true));
    lengthInfo.setFont (fxui::font (13.0f));
    lengthInfo.setColour (juce::Label::textColourId, dimText);
    styleCaption (lengthTitle, "LENGTH");
    styleCaption (manualBpmTitle, "MANUAL BPM");

    manualBpmSlider.setSliderStyle (juce::Slider::LinearBar);
    manualBpmSlider.setTooltip ("Used when the host does not report a tempo");
    manualBpmAttachment = std::make_unique<APVTS::SliderAttachment> (state, ParamIDs::manualBpm, manualBpmSlider);

    lengthUnitBox.addItemList (juce::StringArray::fromTokens (lengthUnitChoices, "|", ""), 1);
    lengthUnitAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, ParamIDs::lengthUnit, lengthUnitBox);

    lengthValueBox.setEditableText (true);
    lengthValueBox.onChange = [this] { commitLengthText(); };

    for (auto* component : std::initializer_list<juce::Component*> { &bpmLabel, &timeSigLabel, &lengthTitle, &lengthInfo, &manualBpmTitle,
                                                                     &manualBpmSlider, &lengthValueBox, &lengthUnitBox, &waveform })
        addAndMakeVisible (component);

    // ---- FX type and macros
    for (int i = 0; i < numFxTypes; ++i)
    {
        auto& button = typeButtons[(size_t) i];
        button.setButtonText (getFxTypeName ((FxType) i));
        button.getProperties().set ("fxType", i);
        button.setTooltip ("Start a new " + juce::String (getFxTypeName ((FxType) i)) + " sound. Loaded samples are removed (Undo brings them back).");
        button.onClick = [this, i]
        {
            audioProcessor.selectFxType ((FxType) i);

            if (audioProcessor.canUndoRandomise())
                showMessage ("Samples removed with the type change - Undo brings them back", false);
        };
        addAndMakeVisible (button);
    }

    for (int i = 0; i < numMacros; ++i)
    {
        macroKnobs[(size_t) i] = std::make_unique<ParameterKnob> (state, ParamIDs::macro (i), "Macro", true);
        addAndMakeVisible (*macroKnobs[(size_t) i]);
    }

    gainKnob = std::make_unique<ParameterKnob> (state, ParamIDs::masterGain, "Gain", false);
    addAndMakeVisible (*gainKnob);
    addAndMakeVisible (meter);

    samplePanel.onSlotSelected = [this] (int slot) { layerEditor.setSampleSlot (slot); };
    samplePanel.onMessage = [this] (const juce::String& text, bool isError) { showMessage (text, isError); };
    addAndMakeVisible (samplePanel);
    addAndMakeVisible (fxPanel);
    addChildComponent (layerEditor);
    addChildComponent (mixerView);
    addChildComponent (presetBrowser);
    presetBrowser.onError = [this] (const juce::String& text) { showMessage (text, true); };
    layerEditor.setMasterSideComponent (&masterScope);

    // ---- Footer

    previewButton.onClick = [this] { audioProcessor.triggerPreview(); };
    previewButton.setTooltip ("Plays the FX once at the current tempo");

    exportButton.onClick = [this] { showExportMenu(); };
    exportButton.setTooltip ("Renders the FX at the current tempo and length to a WAV file");
    addAndMakeVisible (exportButton);

    styleCaption (tailTitle, "TAIL");
    styleCaption (retriggerTitle, "RETRIGGER");
    styleCaption (stoppedTitle, "STOPPED");
    tailBox.addItemList (juce::StringArray::fromTokens (tailModeChoices, "|", ""), 1);
    retriggerBox.addItemList (juce::StringArray::fromTokens (retriggerChoices, "|", ""), 1);
    stoppedBox.addItemList (juce::StringArray::fromTokens (stoppedNotesChoices, "|", ""), 1);
    tailAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, ParamIDs::tailMode, tailBox);
    retriggerAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, ParamIDs::retrigger, retriggerBox);
    stoppedAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, ParamIDs::stoppedNotes, stoppedBox);

    const juce::String stoppedTip = "What a MIDI note does while the host is stopped (for example when you move a note in the piano roll). "
                                    "Full: plays the whole FX. Gate: plays only while the key is held. Off: stays silent. "
                                    "During playback a note always plays its full length.";
    stoppedTitle.setTooltip (stoppedTip);
    stoppedBox.setTooltip (stoppedTip);

    messageLabel.setFont (fxui::font (12.5f));
    messageLabel.setJustificationType (juce::Justification::centredLeft);

    for (auto* component : std::initializer_list<juce::Component*> { &previewButton, &tailTitle, &retriggerTitle, &stoppedTitle,
                                                                     &tailBox, &retriggerBox, &stoppedBox })
        addAndMakeVisible (component);

    addChildComponent (messageLabel);       // shown in place of the footer settings while there is something to say

    // A click on the background takes the focus away from a text field (and
    // globalFocusChanged then hands it back to the host).
    setWantsKeyboardFocus (true);
    juce::Desktop::getInstance().addFocusChangeListener (this);

    rebuildPresetList();
    setSize (editorWidth, editorHeight());
    showPage (mainPage);
    timerCallback();
    startTimerHz (30);
}

FXtendAudioProcessorEditor::~FXtendAudioProcessorEditor()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    juce::PopupMenu::dismissAllActiveMenus();       // they are drawn with this editor's look
    stopTimer();
    setLookAndFeel (nullptr);
}

void FXtendAudioProcessorEditor::globalFocusChanged (juce::Component* focusedComponent)
{
    if (focusedComponent == nullptr || (focusedComponent != this && ! isParentOf (focusedComponent)))
        return;

    // Typing a preset name, a length or a value needs the keyboard.
    if (dynamic_cast<juce::TextEditor*> (focusedComponent) != nullptr)
        return;

    // Buttons, sliders and menus would otherwise keep the focus after a click
    // and swallow keys such as the space bar. With no focused component, keys
    // pass through to the host.
    focusedComponent->giveAwayKeyboardFocus();
}

void FXtendAudioProcessorEditor::showPage (int page)
{
    currentPage = juce::jlimit (0, (int) numPages - 1, page);
    const bool isMain = currentPage == mainPage;

    for (int i = 0; i < numPages; ++i)
        pageButtons[(size_t) i].setToggleState (i == currentPage, juce::dontSendNotification);

    for (auto& knob : macroKnobs)
        knob->setVisible (isMain);

    gainKnob->setVisible (isMain);
    samplePanel.setVisible (isMain);
    fxPanel.setVisible (isMain);
    mixerView.setVisible (currentPage == mixerPage);
    presetBrowser.setVisible (currentPage == presetsPage);
    layerEditor.setVisible (currentPage == advancedPage);
    meter.setVisible (currentPage == mainPage || currentPage == mixerPage);
    meter.toFront (false);      // on the Mixer page it sits on top of the master strip

    resized();
}

//==============================================================================

void FXtendAudioProcessorEditor::rebuildPresetList()
{
    auto& manager = audioProcessor.getPresetManager();
    presetBrowser.refresh();
    presetBox.clear (juce::dontSendNotification);

    int lastType = -1;

    for (int i = 0; i < manager.getNumPresets(); ++i)
    {
        if (i == manager.getNumFactoryPresets())
            presetBox.addSectionHeading ("User");
        else if (manager.isFactory (i) && manager.getName (i) == emptyPresetName)
            presetBox.addSectionHeading ("Empty");
        else if (manager.isFactory (i) && (int) manager.getFxType (i) != lastType)
            presetBox.addSectionHeading (getFxTypeName (manager.getFxType (i)));

        lastType = (int) manager.getFxType (i);
        presetBox.addItem (manager.getName (i), i + 1);
    }

    const auto name = audioProcessor.getCurrentPresetName();
    const int index = manager.indexOf (name);

    if (index >= 0)
        presetBox.setSelectedId (index + 1, juce::dontSendNotification);
    else
        presetBox.setText (name, juce::dontSendNotification);
}

void FXtendAudioProcessorEditor::setSaveMode (bool shouldShow)
{
    saveMode = shouldShow;

    presetBox.setVisible (! saveMode);
    saveButton.setVisible (! saveMode);
    randomButton.setVisible (! saveMode);
    undoButton.setVisible (! saveMode);
    fileButton.setVisible (! saveMode);
    nameEditor.setVisible (saveMode);
    confirmSaveButton.setVisible (saveMode);
    cancelSaveButton.setVisible (saveMode);

    if (! saveMode)
    {
        renameMode = false;
        rebuildPresetList();        // the sound may have changed while the name field was open
    }

    if (saveMode)
    {
        const auto current = audioProcessor.getCurrentPresetName();
        const bool isFactory = audioProcessor.getPresetManager().isFactory (audioProcessor.getPresetManager().indexOf (current));
        nameEditor.setText (isFactory || current.isEmpty() || current.endsWith ("(init)") ? juce::String ("My ") + getFxTypeName (audioProcessor.getCurrentPatch().fxType)
                                                                                          : current,
                            juce::dontSendNotification);
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
    }
}

void FXtendAudioProcessorEditor::commitSave()
{
    const bool renaming = renameMode;
    const auto result = renaming ? audioProcessor.renameCurrentPreset (nameEditor.getText())
                                 : audioProcessor.saveUserPreset (nameEditor.getText());

    if (result.failed())
    {
        showMessage (result.getErrorMessage(), true);
        return;
    }

    setSaveMode (false);
    rebuildPresetList();
    showMessage (juce::String (renaming ? "Renamed to \"" : "Saved \"") + audioProcessor.getCurrentPresetName() + "\"", false);
}

void FXtendAudioProcessorEditor::showMessage (const juce::String& text, bool isError)
{
    messageLabel.setColour (juce::Label::textColourId, isError ? fxui::Colours::danger : fxui::Colours::text);
    messageLabel.setText (text, juce::dontSendNotification);
    messageTicks = 30 * 6;

    // The message takes the place of the settings next to TAIL for a few seconds.
    for (auto* component : std::initializer_list<juce::Component*> { &retriggerTitle, &retriggerBox, &stoppedTitle, &stoppedBox })
        component->setVisible (false);

    messageLabel.setVisible (true);
}

void FXtendAudioProcessorEditor::clearMessage()
{
    messageTicks = 0;
    messageLabel.setText ({}, juce::dontSendNotification);
    messageLabel.setVisible (false);

    for (auto* component : std::initializer_list<juce::Component*> { &retriggerTitle, &retriggerBox, &stoppedTitle, &stoppedBox })
        component->setVisible (true);
}

//==============================================================================

void FXtendAudioProcessorEditor::rebuildLengthChoices()
{
    lengthValueBox.clear (juce::dontSendNotification);

    if (shownLengthUnit == (int) LengthUnit::bars)
        lengthValueBox.addItemList ({ "1/2", "1", "2", "4", "8", "16" }, 1);
    else if (shownLengthUnit == (int) LengthUnit::beats)
        lengthValueBox.addItemList ({ "1/2", "1", "2", "4", "8" }, 1);
    else
        lengthValueBox.addItemList ({ "0.25", "1/2", "1", "2", "3", "4", "8", "16" }, 1);
}

void FXtendAudioProcessorEditor::commitLengthText()
{
    const double value = parseLength (lengthValueBox.getText());

    if (value <= 0.0)
    {
        showMessage ("Length must be a number such as 4, 0.5 or 1/2.", true);
        refreshLengthText();
        return;
    }

    if (auto* parameter = audioProcessor.getParameters().getParameter (ParamIDs::lengthValue))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
        parameter->endChangeGesture();
    }

    shownLengthValue = -1.0f;   // show what the parameter actually accepted
}

void FXtendAudioProcessorEditor::refreshLengthText()
{
    lengthValueBox.setText (formatNumber (shownLengthValue), juce::dontSendNotification);
}

void FXtendAudioProcessorEditor::refreshInfo()
{
    const auto& status = audioProcessor.getEngine().getStatus();
    const auto settings = audioProcessor.getCurrentSettings();

    const double bpm = status.bpm.load();
    const bool tempoFromHost = status.tempoFromHost.load();
    const TimeSignature signature { status.timeSigNumerator.load(), status.timeSigDenominator.load() };
    const bool signatureFromHost = status.timeSigFromHost.load();

    const double quarterNotes = FxEngine::lengthInQuarterNotes (settings.lengthUnit, settings.lengthValue, signature);
    const double bars = quarterNotes / (signature.numerator * 4.0 / signature.denominator);
    const double seconds = quarterNotes * 60.0 / juce::jmax (1.0, bpm);

    juce::String info;
    info << juce::String (bpm, 1) << (tempoFromHost ? " H " : " M ") << signature.numerator << "/" << signature.denominator
         << (signatureFromHost ? " H " : " F ") << quarterNotes << " " << seconds;

    if (info == shownInfo)
        return;

    shownInfo = info;

    bpmLabel.setText ("BPM " + juce::String (bpm, 1) + (tempoFromHost ? "  HOST" : "  MANUAL"), juce::dontSendNotification);
    bpmLabel.setColour (juce::Label::textColourId, tempoFromHost ? fxui::Colours::text : fxui::Colours::warning);
    bpmLabel.setTooltip (tempoFromHost ? juce::String() : "The host is not reporting a tempo, so the manual BPM is used.");

    timeSigLabel.setText (juce::String (signature.numerator) + "/" + juce::String (signature.denominator)
                          + (signatureFromHost ? "" : "  (default)"), juce::dontSendNotification);
    timeSigLabel.setColour (juce::Label::textColourId, signatureFromHost ? fxui::Colours::text : fxui::Colours::warning);

    manualBpmSlider.setAlpha (tempoFromHost ? 0.45f : 1.0f);
    manualBpmTitle.setAlpha (tempoFromHost ? 0.45f : 1.0f);

    lengthInfo.setText (formatNumber (bars) + (std::abs (bars - 1.0) < 1.0e-6 ? " bar  /  " : " bars  /  ")
                        + formatNumber (quarterNotes) + " quarter notes  /  " + juce::String (seconds, 2) + " s",
                        juce::dontSendNotification);
}

void FXtendAudioProcessorEditor::refreshFxType()
{
    const auto type = audioProcessor.getCurrentPatch().fxType;

    if ((int) type == shownFxType)
        return;

    shownFxType = (int) type;
    const auto accent = fxui::getAccent (type);
    theme.setAccent (accent);
    waveform.setAccent (accent);

    for (int i = 0; i < numFxTypes; ++i)
        typeButtons[(size_t) i].setToggleState (i == shownFxType, juce::dontSendNotification);

    for (int i = 0; i < numMacros; ++i)
        macroKnobs[(size_t) i]->setTitle (getMacroName (type, i));

    sendLookAndFeelChange();
    repaint();
}

void FXtendAudioProcessorEditor::requestPreviewIfNeeded()
{
    RenderRequest request = audioProcessor.makeRenderRequest();

    // While a preset is auditioned the picture shows that preset, at the
    // current length and tempo, and says so.
    const bool auditioning = audioProcessor.getAuditionIndex() >= 0;

    if (auditioning)
        request.patch = audioProcessor.getAuditionPatch();

    waveform.setAudition (auditioning ? audioProcessor.getAuditionName() : juce::String());

    if (shownFxType >= 0)
        waveform.setAccent (fxui::getAccent (auditioning ? request.patch.fxType : (FxType) shownFxType));

    request.patch.masterGainDb = 0.0f;

    const bool same = hasRequest
                      && std::memcmp (&request.patch, &lastRequest.patch, sizeof (Patch)) == 0
                      && request.settings.lengthUnit == lastRequest.settings.lengthUnit
                      && juce::exactlyEqual (request.settings.lengthValue, lastRequest.settings.lengthValue)
                      && juce::exactlyEqual (request.bpm, lastRequest.bpm)
                      && request.timeSig.numerator == lastRequest.timeSig.numerator
                      && request.timeSig.denominator == lastRequest.timeSig.denominator
                      && request.samples == lastRequest.samples
                      && request.stretched == lastRequest.stretched;

    if (! same)
    {
        lastRequest = request;
        hasRequest = true;
        waveform.setRequest (request);
    }
}

void FXtendAudioProcessorEditor::timerCallback()
{
    const auto settings = audioProcessor.getCurrentSettings();

    if ((int) settings.lengthUnit != shownLengthUnit)
    {
        shownLengthUnit = (int) settings.lengthUnit;
        rebuildLengthChoices();
        shownLengthValue = -1.0f;
    }

    if (! juce::exactlyEqual ((float) settings.lengthValue, shownLengthValue) && ! lengthValueBox.hasKeyboardFocus (true))
    {
        shownLengthValue = (float) settings.lengthValue;
        refreshLengthText();
    }

    if (audioProcessor.getPatchRevision() != shownPatchRevision)
    {
        shownPatchRevision = audioProcessor.getPatchRevision();

        if (! saveMode)
            rebuildPresetList();
    }

    undoButton.setEnabled (audioProcessor.canUndoRandomise());

    refreshFxType();
    refreshInfo();
    requestPreviewIfNeeded();
    const auto& status = audioProcessor.getEngine().getStatus();
    waveform.setPlayhead (status.phase.load());
    meter.update (status.peakLeft.load(), status.peakRight.load(), status.clipCount.load());

    if (messageTicks > 0 && --messageTicks == 0)
        clearMessage();
}

//==============================================================================

void FXtendAudioProcessorEditor::showFileMenu()
{
    // Menu actions run after the menu has closed; the host may have closed the
    // window in between, so they go through a pointer that knows.
    const auto safeThis = juce::Component::SafePointer<FXtendAudioProcessorEditor> (this);
    const auto whenAlive = [safeThis] (std::function<void (FXtendAudioProcessorEditor&)> action)
    {
        return [safeThis, action = std::move (action)] { if (safeThis != nullptr) action (*safeThis); };
    };

    juce::PopupMenu menu;
    menu.addItem ("Export Preset Package...", whenAlive ([] (auto& editor) { editor.choosePackage (false); }));
    menu.addItem ("Import Preset Package...", whenAlive ([] (auto& editor) { editor.choosePackage (true); }));
    menu.addSeparator();

    const bool isUserPreset = audioProcessor.isCurrentPresetUserPreset();
    menu.addItem ("Rename Preset...", isUserPreset, false, whenAlive ([] (auto& editor)
    {
        editor.renameMode = true;
        editor.setSaveMode (true);
    }));
    menu.addItem ("Delete Preset", isUserPreset, false, whenAlive ([] (auto& editor)
    {
        const auto name = editor.audioProcessor.getCurrentPresetName();
        const auto result = editor.audioProcessor.deleteCurrentPreset();
        editor.rebuildPresetList();
        editor.showMessage (result.wasOk() ? "Moved \"" + name + "\" to the Trash" : result.getErrorMessage(), result.failed());
    }));

    menu.addSeparator();
    menu.addItem ("Show Preset Folder", whenAlive ([] (auto& editor)
    {
        const auto folder = editor.audioProcessor.getPresetManager().getUserDirectory();
        folder.createDirectory();
        folder.revealToUser();
    }));

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileButton));
}

void FXtendAudioProcessorEditor::choosePackage (bool isImport)
{
    const juce::String pattern = juce::String ("*") + PresetPackage::fileExtension;
    const auto safeThis = juce::Component::SafePointer<FXtendAudioProcessorEditor> (this);

    if (isImport)
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Import a preset package", juce::File(), pattern);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();

                                      if (safeThis == nullptr || file == juce::File())
                                          return;

                                      const auto result = safeThis->audioProcessor.importPresetPackage (file);
                                      safeThis->rebuildPresetList();
                                      safeThis->showMessage (result.wasOk() ? "Imported \"" + safeThis->audioProcessor.getCurrentPresetName() + "\""
                                                                            : result.getErrorMessage(),
                                                             result.failed());
                                  });
        return;
    }

    const auto name = defaultExportName ("FXtend Preset");

    fileChooser = std::make_unique<juce::FileChooser> ("Export a preset package",
                                                       juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                           .getChildFile (juce::File::createLegalFileName (name) + PresetPackage::fileExtension),
                                                       pattern);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis] (const juce::FileChooser& chooser)
                              {
                                  auto file = chooser.getResult();

                                  if (safeThis == nullptr || file == juce::File())
                                      return;

                                  file = file.withFileExtension (PresetPackage::fileExtension);
                                  const auto result = safeThis->audioProcessor.exportPresetPackage (file);
                                  safeThis->showMessage (result.wasOk() ? "Exported " + file.getFileName() : result.getErrorMessage(),
                                                         result.failed());
                              });
}

void FXtendAudioProcessorEditor::showExportMenu()
{
    if (exporting)
        return;

    const auto safeThis = juce::Component::SafePointer<FXtendAudioProcessorEditor> (this);

    // Choosing an option changes it and shows the menu again.
    const auto option = [safeThis] (std::function<void (fxtend::WavExportOptions&)> change)
    {
        return [safeThis, change = std::move (change)]
        {
            if (safeThis != nullptr)
            {
                change (safeThis->exportOptions);
                safeThis->showExportMenu();
            }
        };
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("Sample rate");

    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        menu.addItem (juce::String (rate / 1000.0, 1) + " kHz", true, juce::exactlyEqual (exportOptions.sampleRate, rate),
                      option ([rate] (auto& options) { options.sampleRate = rate; }));

    menu.addSectionHeader ("Format");
    menu.addItem ("24-bit PCM", true, ! exportOptions.floatingPoint, option ([] (auto& options) { options.floatingPoint = false; }));
    menu.addItem ("32-bit float", true, exportOptions.floatingPoint, option ([] (auto& options) { options.floatingPoint = true; }));

    menu.addSectionHeader ("Velocity");

    for (const int velocity : { 64, 100, 127 })
        menu.addItem (juce::String (velocity), true, exportOptions.velocity == velocity,
                      option ([velocity] (auto& options) { options.velocity = velocity; }));

    menu.addSeparator();
    menu.addItem ("Save WAV...", [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->chooseWavFile();
    });

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (exportButton));
}

void FXtendAudioProcessorEditor::chooseWavFile()
{
    const auto& status = audioProcessor.getEngine().getStatus();
    const auto name = defaultExportName ("FXtend") + " " + juce::String (status.bpm.load(), 0) + "bpm";

    fileChooser = std::make_unique<juce::FileChooser> ("Export WAV",
                                                       juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                           .getChildFile (juce::File::createLegalFileName (name) + ".wav"),
                                                       "*.wav");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis = juce::Component::SafePointer<FXtendAudioProcessorEditor> (this)] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  if (safeThis != nullptr && file != juce::File())
                                      safeThis->exportWavTo (file.withFileExtension ("wav"));
                              });
}

juce::String FXtendAudioProcessorEditor::defaultExportName (const juce::String& fallback) const
{
    // "Riser (init)" is a starting point, not a name the user chose.
    const auto name = audioProcessor.getCurrentPresetName().upToFirstOccurrenceOf (" (init)", false, false);
    return name.isEmpty() ? fallback : name;
}

void FXtendAudioProcessorEditor::exportWavTo (const juce::File& target)
{
    // The request holds everything the render needs (including the sample
    // audio), so the work can run in the background without touching the plugin.
    auto request = audioProcessor.makeRenderRequest();
    const auto options = exportOptions;
    const auto safeThis = juce::Component::SafePointer<FXtendAudioProcessorEditor> (this);

    exporting = true;
    exportButton.setEnabled (false);
    showMessage ("Exporting " + target.getFileName() + "...", false);

    juce::Thread::launch ([job = std::move (request), options, target, safeThis]
    {
        const auto result = exportWav (job, target, options);

        juce::MessageManager::callAsync ([safeThis, result, target]
        {
            if (safeThis == nullptr)
                return;

            safeThis->exporting = false;
            safeThis->exportButton.setEnabled (true);
            safeThis->showMessage (result.wasOk() ? "Exported " + target.getFileName() : result.getErrorMessage(), result.failed());
        });
    });
}

//==============================================================================

void FXtendAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (fxui::Colours::background.brighter (0.06f), 0.0f, 0.0f,
                                             fxui::Colours::background.darker (0.25f), 0.0f, bounds.getBottom(), false));
    g.fillAll();

    // A little of the current type's colour in the air behind the header.
    const auto accent = theme.getAccent();
    juce::ColourGradient glow (accent.withAlpha (0.16f), bounds.getCentreX(), -40.0f,
                               accent.withAlpha (0.0f), bounds.getCentreX(), 260.0f, true);
    g.setGradientFill (glow);
    g.fillRect (bounds.withHeight (300.0f));
}

void FXtendAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    // Header
    auto header = bounds.removeFromTop (headerHeight).reduced (0, 5);
    logo.setBounds (header.removeFromLeft (108).withTrimmedRight (6));

    for (auto& button : pageButtons)
        button.setBounds (header.removeFromLeft (button.getButtonText().length() > 7 ? 84 : (button.getButtonText().length() > 6 ? 72 : 58)));

    header.removeFromLeft (8);

    auto headerRight = header.removeFromRight (310);
    fileButton.setBounds (headerRight.removeFromRight (60).reduced (3, 0));
    undoButton.setBounds (headerRight.removeFromRight (70).reduced (3, 0));
    randomButton.setBounds (headerRight.removeFromRight (90).reduced (3, 0));
    saveButton.setBounds (headerRight.removeFromRight (80).reduced (3, 0));
    presetBox.setBounds (header.reduced (3, 0));

    auto saveArea = presetBox.getBounds().getUnion (fileButton.getBounds());
    cancelSaveButton.setBounds (saveArea.removeFromRight (90).reduced (3, 0));
    confirmSaveButton.setBounds (saveArea.removeFromRight (70).reduced (3, 0));
    nameEditor.setBounds (saveArea.reduced (0, 1));

    bounds.removeFromTop (gap);

    // FX type: what kind of sound this is, so it sits above everything it changes.
    auto types = bounds.removeFromTop (typeHeight);
    const int typeWidth = types.getWidth() / numFxTypes;

    for (auto& button : typeButtons)
        button.setBounds (types.removeFromLeft (typeWidth).reduced (3, 2));

    bounds.removeFromTop (gap);

    // Tempo and length
    auto tempo = bounds.removeFromTop (tempoHeight).reduced (0, 4);
    bpmLabel.setBounds (tempo.removeFromLeft (150));
    timeSigLabel.setBounds (tempo.removeFromLeft (100));
    lengthTitle.setBounds (tempo.removeFromLeft (60));
    tempo.removeFromLeft (6);
    lengthValueBox.setBounds (tempo.removeFromLeft (70));
    tempo.removeFromLeft (4);
    lengthUnitBox.setBounds (tempo.removeFromLeft (130));
    manualBpmSlider.setBounds (tempo.removeFromRight (80));
    manualBpmTitle.setBounds (tempo.removeFromRight (90));
    tempo.removeFromLeft (10);
    lengthInfo.setBounds (tempo);

    bounds.removeFromTop (gap);
    // Footer (taken from the bottom so that every page gets the same middle area)
    auto footerArea = bounds.removeFromBottom (footerHeight);
    bounds.removeFromBottom (gap);

    if (currentPage == mainPage)
    {
        auto page = bounds;
        const int fixed = macroHeight + fxui::SamplePanel::getPreferredHeight() + fxui::FxPanel::getPreferredHeight() + gap * 3;
        waveform.setBounds (page.removeFromTop (page.getHeight() - fixed));
        page.removeFromTop (gap);

        auto macros = page.removeFromTop (macroHeight);
        meter.setBounds (macros.removeFromRight (22).reduced (0, 6));
        macros.removeFromRight (4);
        gainKnob->setBounds (macros.removeFromRight (110).reduced (8, 0));
        macros.removeFromRight (20);
        const int knobWidth = macros.getWidth() / numMacros;

        for (auto& knob : macroKnobs)
            knob->setBounds (macros.removeFromLeft (knobWidth).reduced (30, 0));

        page.removeFromTop (gap);
        samplePanel.setBounds (page.removeFromTop (fxui::SamplePanel::getPreferredHeight()));
        page.removeFromTop (gap);
        fxPanel.setBounds (page);
    }
    else
    {
        waveform.setBounds (bounds.removeFromTop (compactWaveformHeight));
        bounds.removeFromTop (gap);
        layerEditor.setBounds (bounds);
        mixerView.setBounds (bounds);
        presetBrowser.setBounds (bounds);

        if (currentPage == mixerPage)
            meter.setBounds (mixerView.getMeterArea().translated (mixerView.getX(), mixerView.getY()));
    }

    auto footer = footerArea.reduced (0, 3);
    tailTitle.setBounds (footer.removeFromLeft (50));
    footer.removeFromLeft (6);
    tailBox.setBounds (footer.removeFromLeft (100));
    previewButton.setBounds (footer.removeFromRight (100));
    footer.removeFromRight (6);
    exportButton.setBounds (footer.removeFromRight (110));
    footer.removeFromRight (10);

    // Messages appear over the settings to the right of TAIL (see showMessage).
    messageLabel.setBounds (footer.withTrimmedLeft (14));

    retriggerTitle.setBounds (footer.removeFromLeft (90));
    footer.removeFromLeft (6);
    retriggerBox.setBounds (footer.removeFromLeft (100));
    stoppedTitle.setBounds (footer.removeFromLeft (80));
    footer.removeFromLeft (6);
    stoppedBox.setBounds (footer.removeFromLeft (84));
}
