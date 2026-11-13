#include "SamplePanel.h"

using namespace fxtend;

namespace fxui
{

namespace
{
    constexpr int slotRowHeight = 28;
    constexpr int thumbnailColumns = 160;

    // The settings shown directly in the main view; the rest are under Advanced > Sample.
    constexpr const char* quickFields[] = { "mode", "trimStart", "trimEnd", "reverse", "pitch", "level" };

    const SampleField* findField (const juce::String& id)
    {
        for (const auto& field : getSampleLayerFields())
            if (id == field.id)
                return &field;

        return nullptr;
    }

    juce::String shortened (const juce::String& name, int maxLength)
    {
        return name.length() <= maxLength ? name : name.substring (0, maxLength - 1) + juce::String::fromUTF8 ("\xe2\x80\xa6");
    }
}

void SamplePanel::SlotButton::setState (bool holdsSampleNow, bool isSelected)
{
    if (holdsSampleNow != holdsSample || isSelected != selected)
    {
        holdsSample = holdsSampleNow;
        selected = isSelected;
        repaint();
    }
}

void SamplePanel::SlotButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto* theme = dynamic_cast<Theme*> (&getLookAndFeel());
    const auto accent = theme != nullptr ? theme->getAccent() : Colours::text;
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    auto fill = holdsSample ? accent : Colours::control;

    if (down)
        fill = fill.brighter (0.2f);
    else if (highlighted)
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 7.0f);

    if (selected)
    {
        g.setColour (Colours::text);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.5f);
    }

    g.setColour (holdsSample ? Colours::background : (selected ? Colours::text : Colours::dimText));
    g.setFont (font (13.0f));
    g.drawFittedText (getButtonText(), getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
}

void SamplePanel::SlotButton::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();

        return;
    }

    juce::TextButton::mouseDown (event);
}

void SamplePanel::SlotButton::mouseDrag (const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu())
        juce::TextButton::mouseDrag (event);
}

void SamplePanel::SlotButton::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu())
        juce::TextButton::mouseUp (event);
}

SamplePanel::SamplePanel (FXtendAudioProcessor& processorToUse) : processor (processorToUse)
{
    for (int i = 0; i < numSampleLayers; ++i)
    {
        auto& button = slotButtons[(size_t) i];

        button.onClick = [this, i]
        {
            const bool wasSelected = i == selectedSlot;
            selectSlot (i);

            // An empty slot is a "load" button; clicking the slot that is
            // already selected asks for a file to put in its place.
            const auto info = processor.getSampleInfo (i);

            if (wasSelected || info.ref.isEmpty())
                chooseFile (i);
        };

        button.onMenu = [this, i] { showSlotMenu (i); };
        addAndMakeVisible (button);

        auto& toggle = slotToggles[(size_t) i];
        toggle.setTooltip ("Sample " + juce::String (i + 1) + " on / off");
        toggle.onStateChange = [this, i]
        {
            // Also fires when the mouse merely moves over the button.
            const bool isOn = slotToggles[(size_t) i].getToggleState();

            if (isOn != shownToggleStates[(size_t) i])
            {
                shownToggleStates[(size_t) i] = isOn;
                shownRevision = -1;
            }
        };
        addAndMakeVisible (toggle);

        if (const auto* onField = findField ("on"))
            toggleAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.getParameters(), ParamIDs::sampleLayer (i, *onField), toggle);
    }

    loadButton.onClick = [this] { chooseFile (selectedSlot); };
    clearButton.onClick = [this] { processor.clearSample (selectedSlot); };
    loadButton.setTooltip ("Load a WAV or AIFF into the selected slot (or drop files onto a slot)");
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);

    statusLabel.setFont (font (12.5f));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (statusLabel);

    rebuildControls();
    refresh();
    startTimerHz (10);
}

SamplePanel::~SamplePanel() = default;

int SamplePanel::getPreferredHeight() noexcept
{
    return 6 + slotRowHeight + 4 + fieldRowHeight + 4;
}

void SamplePanel::selectSlot (int slot)
{
    if (slot == selectedSlot)
        return;

    selectedSlot = slot;
    rebuildControls();
    shownRevision = -1;
    refresh();

    if (onSlotSelected)
        onSlotSelected (slot);
}

void SamplePanel::rebuildControls()
{
    controls.clear();

    for (const char* id : quickFields)
    {
        if (const auto* field = findField (id))
        {
            controls.push_back (std::make_unique<FieldControl> (processor.getParameters(),
                                                                FieldControl::describe (*field, ParamIDs::sampleLayer (selectedSlot, *field))));
            addAndMakeVisible (*controls.back());
        }
    }

    resized();
}

void SamplePanel::refresh()
{
    for (int i = 0; i < numSampleLayers; ++i)
    {
        const auto info = processor.getSampleInfo (i);
        juce::String text = "+ Load WAV";

        if (info.missing)
            text = "Missing: " + shortened (info.ref.name, 10);
        else if (info.loaded)
            text = shortened (info.ref.name, 17);

        auto& button = slotButtons[(size_t) i];
        button.setButtonText (text);
        button.setState (! info.ref.isEmpty(), i == selectedSlot);
        button.setTooltip (info.ref.isEmpty() ? juce::String ("Sample slot ") + juce::String (i + 1) + " - click to load"
                                              : info.ref.name + "\n" + info.ref.originalPath
                                                    + "\nClick again, or drop a file here, to replace it"
                                                      "\nRight-click for Clear and more");
        button.setAlpha (slotToggles[(size_t) i].getToggleState() || info.ref.isEmpty() ? 1.0f : 0.45f);
        slotToggles[(size_t) i].setVisible (! info.ref.isEmpty());     // nothing to switch off in an empty slot
    }

    const auto info = processor.getSampleInfo (selectedSlot);
    juce::String status;
    auto colour = Colours::dimText;

    if (info.missing)
    {
        status = "Missing sample - Load to re-link";
        colour = Colours::danger;
    }
    else if (! info.loaded)
    {
        status = "Drop a WAV or AIFF here";
    }
    else
    {
        status = juce::String (info.seconds, 2) + " s";

        if (info.stretchMode)
        {
            status << "  x" << juce::String (info.stretchRatio, 2);

            if (info.stretchExtreme)
            {
                status << "  extreme stretch";
                colour = Colours::warning;
            }
            else if (! info.stretchReady)
            {
                status << "  stretching...";
                colour = Colours::warning;
            }
        }
    }

    statusLabel.setText (status, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, colour);
    clearButton.setEnabled (! info.ref.isEmpty());
    loadButton.setButtonText (info.ref.isEmpty() ? "Load..." : "Replace...");

    if (info.loaded && ! slotToggles[(size_t) selectedSlot].getToggleState())
        statusLabel.setText ("Off  -  " + status, juce::dontSendNotification);

    const bool usable = info.loaded;

    for (auto& control : controls)
        control->setRelevant (usable);

    rebuildThumbnail();
}

void SamplePanel::rebuildThumbnail()
{
    thumbnail.clear();

    if (const auto data = processor.getSampleData (selectedSlot))
    {
        const int frames = data->getNumFrames();
        thumbnail.assign ((size_t) thumbnailColumns, 0.0f);

        for (int column = 0; column < thumbnailColumns && frames > 0; ++column)
        {
            const int from = (int) ((juce::int64) column * frames / thumbnailColumns);
            const int count = juce::jmax (1, (int) ((juce::int64) (column + 1) * frames / thumbnailColumns) - from);
            thumbnail[(size_t) column] = juce::jmin (1.0f, data->audio.getMagnitude (from, juce::jmin (count, frames - from)));
        }
    }

    repaint (thumbnailArea);
}

void SamplePanel::timerCallback()
{
    const auto patch = processor.getCurrentPatch();
    const auto& params = patch.samples[(size_t) selectedSlot];

    if (processor.getSampleRevision() != shownRevision)
    {
        shownRevision = processor.getSampleRevision();
        refresh();
    }

    if (! juce::exactlyEqual (params.trimStart, shownTrimStart) || ! juce::exactlyEqual (params.trimEnd, shownTrimEnd)
        || params.isReversed() != shownReverse)
    {
        shownTrimStart = params.trimStart;
        shownTrimEnd = params.trimEnd;
        shownReverse = params.isReversed();
        repaint (thumbnailArea);
    }
}

//==============================================================================

void SamplePanel::chooseFile (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into slot " + juce::String (slot + 1),
                                                   juce::File(), "*.wav;*.aif;*.aiff");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<SamplePanel> (this), slot] (const juce::FileChooser& finished)
                          {
                              const auto file = finished.getResult();

                              if (safeThis != nullptr && file != juce::File())
                                  safeThis->loadFiles ({ file.getFullPathName() }, slot);
                          });
}

void SamplePanel::showSlotMenu (int slot)
{
    selectSlot (slot);

    const auto info = processor.getSampleInfo (slot);
    const bool filled = ! info.ref.isEmpty();
    const bool on = slotToggles[(size_t) slot].getToggleState();
    const auto safeThis = juce::Component::SafePointer<SamplePanel> (this);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Slot " + juce::String (slot + 1) + (filled ? "  -  " + shortened (info.ref.name, 28) : juce::String()));

    menu.addItem (filled ? "Replace..." : "Load...", [safeThis, slot]
    {
        if (safeThis != nullptr)
            safeThis->chooseFile (slot);
    });

    menu.addItem (on ? "Turn Off" : "Turn On", filled, false, [safeThis, slot, on]
    {
        if (safeThis != nullptr)
            safeThis->slotToggles[(size_t) slot].setToggleState (! on, juce::sendNotificationSync);
    });

    menu.addSeparator();

    menu.addItem ("Clear", filled, false, [safeThis, slot]
    {
        if (safeThis != nullptr)
            safeThis->processor.clearSample (slot);
    });

    bool anyFilled = false;

    for (int i = 0; i < numSampleLayers; ++i)
        anyFilled = anyFilled || ! processor.getSampleInfo (i).ref.isEmpty();

    menu.addItem ("Clear All Slots", anyFilled, false, [safeThis]
    {
        if (safeThis != nullptr)
            for (int i = 0; i < numSampleLayers; ++i)
                safeThis->processor.clearSample (i);
    });

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotButtons[(size_t) slot]));
}

void SamplePanel::loadFiles (const juce::StringArray& files, int firstSlot)
{
    int slot = firstSlot;

    for (const auto& path : files)
    {
        if (slot >= numSampleLayers)
        {
            if (onMessage)
                onMessage ("There are only " + juce::String (numSampleLayers) + " sample slots; the remaining files were not loaded.", true);

            break;
        }

        const auto result = processor.loadSample (slot, juce::File (path));

        if (result.failed())
        {
            if (onMessage)
                onMessage (result.getErrorMessage(), true);

            continue;
        }

        ++slot;
    }

    if (slot > firstSlot)
        selectSlot (firstSlot);

    shownRevision = -1;
}

int SamplePanel::slotAt (int x, int y) const
{
    for (int i = 0; i < numSampleLayers; ++i)
        if (slotButtons[(size_t) i].getBounds().contains (x, y))
            return i;

    return -1;
}

bool SamplePanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (SampleManager::hasSupportedExtension (juce::File (path)))
            return true;

    return false;
}

void SamplePanel::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void SamplePanel::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int over = slotAt (x, y);
    const int target = over >= 0 ? over : selectedSlot;

    if (target != dragTargetSlot)
    {
        dragTargetSlot = target;
        repaint();
    }
}

void SamplePanel::fileDragExit (const juce::StringArray&)
{
    dragTargetSlot = -1;
    repaint();
}

void SamplePanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int over = slotAt (x, y);
    dragTargetSlot = -1;
    repaint();

    juce::StringArray audioFiles;

    for (const auto& path : files)
        if (SampleManager::hasSupportedExtension (juce::File (path)))
            audioFiles.add (path);

    loadFiles (audioFiles, over >= 0 ? over : selectedSlot);
}

//==============================================================================

void SamplePanel::paint (juce::Graphics& g)
{
    paintPanel (g, getLocalBounds().toFloat());

    g.setFont (font (10.5f, true));
    g.setColour (Colours::dimText);
    g.drawText ("SAMPLE", 10, 6, fieldTitleWidth - 10, slotRowHeight, juce::Justification::centredLeft);
    g.drawText ("SLOT " + juce::String (selectedSlot + 1), 10, 6 + slotRowHeight + 4, fieldTitleWidth - 10, fieldRowHeight,
                juce::Justification::centredLeft);

    if (dragTargetSlot >= 0)
    {
        auto* theme = dynamic_cast<Theme*> (&getLookAndFeel());
        g.setColour (theme != nullptr ? theme->getAccent() : Colours::text);
        g.drawRoundedRectangle (slotButtons[(size_t) dragTargetSlot].getBounds().toFloat().expanded (2.0f), 5.0f, 2.0f);
    }

    // Thumbnail of the selected sample with the trimmed-away parts dimmed.
    const auto area = thumbnailArea.toFloat();
    g.setColour (Colours::control.withAlpha (0.6f));
    g.fillRoundedRectangle (area, 3.0f);

    if (! thumbnail.empty())
    {
        auto* theme = dynamic_cast<Theme*> (&getLookAndFeel());
        const auto accent = theme != nullptr ? theme->getAccent() : Colours::text;
        const float columnWidth = area.getWidth() / (float) thumbnail.size();
        const float trimStart = juce::jlimit (0.0f, 1.0f, shownTrimStart);
        const float trimEnd = juce::jlimit (trimStart, 1.0f, shownTrimEnd);

        for (size_t column = 0; column < thumbnail.size(); ++column)
        {
            const float position = ((float) column + 0.5f) / (float) thumbnail.size();
            const float height = juce::jmax (1.0f, thumbnail[column] * (area.getHeight() - 4.0f));
            const bool used = position >= trimStart && position <= trimEnd;

            g.setColour (used ? accent : Colours::dimText.withAlpha (0.35f));
            g.fillRect (area.getX() + (float) column * columnWidth, area.getCentreY() - height * 0.5f, columnWidth + 0.3f, height);
        }

        if (shownReverse)
        {
            g.setColour (Colours::text);
            g.setFont (font (10.0f, true));
            g.drawText ("REV", area.reduced (4.0f, 2.0f), juce::Justification::topRight);
        }
    }
}

void SamplePanel::resized()
{
    auto bounds = getLocalBounds().reduced (6, 0);
    bounds.removeFromTop (6);

    auto slotRow = bounds.removeFromTop (slotRowHeight);
    slotRow.removeFromLeft (fieldTitleWidth - 6);

    for (size_t i = 0; i < slotButtons.size(); ++i)
    {
        auto cell = slotRow.removeFromLeft (132).reduced (2, 0);
        slotToggles[i].setBounds (cell.removeFromLeft (24));
        slotButtons[i].setBounds (cell);
    }

    slotRow.removeFromLeft (6);
    loadButton.setBounds (slotRow.removeFromLeft (84).reduced (2, 0));
    clearButton.setBounds (slotRow.removeFromLeft (60).reduced (2, 0));
    slotRow.removeFromLeft (8);
    statusLabel.setBounds (slotRow);

    bounds.removeFromTop (4);
    auto controlRow = bounds.removeFromTop (fieldRowHeight);
    controlRow.removeFromLeft (fieldTitleWidth - 6);
    const int cellWidth = controlRow.getWidth() / fieldColumns;

    for (auto& control : controls)
        control->setBounds (controlRow.removeFromLeft (cellWidth));

    thumbnailArea = controlRow.reduced (4, 5);
}

} // namespace fxui
