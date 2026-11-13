#include "PresetManager.h"
#include "FactoryPresets.h"
#include "../Storage.h"

namespace fxtend
{

PresetManager::PresetManager() : PresetManager (getDefaultUserDirectory()) {}

PresetManager::PresetManager (const juce::File& directory) : userDirectory (directory)
{
    refresh();
}

juce::File PresetManager::getDefaultUserDirectory()
{
    return getAppDataDirectory().getChildFile ("Presets");
}

void PresetManager::refresh()
{
    entries.clear();

    for (const auto& preset : getFactoryPresets())
        entries.push_back ({ preset.name, preset.patch.fxType, {} });

    numFactory = (int) entries.size();

    auto files = userDirectory.findChildFiles (juce::File::findFiles, false,
                                               juce::String ("*") + PresetSerializer::fileExtension);
    files.sort();

    for (const auto& file : files)
    {
        PresetData preset;

        if (PresetSerializer::fromJson (file.loadFileAsString(), preset).wasOk())
            entries.push_back ({ file.getFileNameWithoutExtension(), preset.patch.fxType, file });
    }
}

juce::String PresetManager::getName (int index) const
{
    return juce::isPositiveAndBelow (index, getNumPresets()) ? entries[(size_t) index].name : juce::String();
}

FxType PresetManager::getFxType (int index) const
{
    return juce::isPositiveAndBelow (index, getNumPresets()) ? entries[(size_t) index].fxType : FxType::riser;
}

int PresetManager::indexOf (const juce::String& name) const
{
    for (int i = 0; i < getNumPresets(); ++i)
        if (entries[(size_t) i].name == name)
            return i;

    return -1;
}

juce::Result PresetManager::load (int index, PresetData& preset) const
{
    if (! juce::isPositiveAndBelow (index, getNumPresets()))
        return juce::Result::fail ("No such preset.");

    if (isFactory (index))
    {
        preset = getFactoryPresets()[(size_t) index];
        return juce::Result::ok();
    }

    const auto& entry = entries[(size_t) index];

    if (! entry.file.existsAsFile())
        return juce::Result::fail ("The preset file is missing: " + entry.file.getFullPathName());

    const auto result = PresetSerializer::fromJson (entry.file.loadFileAsString(), preset);

    if (result.wasOk())
        preset.name = entry.name;       // the file name is the name the user sees

    return result;
}

juce::File PresetManager::fileFor (const juce::String& name) const
{
    return userDirectory.getChildFile (name + PresetSerializer::fileExtension);
}

juce::Result PresetManager::checkName (const juce::String& name) const
{
    if (name.trim().isEmpty())
        return juce::Result::fail ("Please enter a preset name.");

    if (name != juce::File::createLegalFileName (name))
        return juce::Result::fail ("The name contains characters that cannot be used in a file name.");

    for (int i = 0; i < numFactory; ++i)
        if (entries[(size_t) i].name == name)
            return juce::Result::fail ("A factory preset already has this name.");

    return juce::Result::ok();
}

juce::Result PresetManager::saveUser (const juce::String& rawName, const PresetData& preset)
{
    const auto name = rawName.trim();

    if (const auto check = checkName (name); check.failed())
        return check;

    if (! userDirectory.isDirectory() && userDirectory.createDirectory().failed())
        return juce::Result::fail ("The preset folder could not be created: " + userDirectory.getFullPathName());

    PresetData toSave = preset;
    toSave.name = name;

    if (! fileFor (name).replaceWithText (PresetSerializer::toJson (toSave)))
        return juce::Result::fail ("The preset could not be written.");

    refresh();
    return juce::Result::ok();
}

juce::Result PresetManager::renameUser (int index, const juce::String& rawName)
{
    const auto newName = rawName.trim();

    if (isFactory (index) || ! juce::isPositiveAndBelow (index, getNumPresets()))
        return juce::Result::fail ("Factory presets cannot be renamed.");

    if (const auto check = checkName (newName); check.failed())
        return check;

    const auto target = fileFor (newName);

    if (target.existsAsFile())
        return juce::Result::fail ("A preset with this name already exists.");

    if (! entries[(size_t) index].file.moveFileTo (target))
        return juce::Result::fail ("The preset could not be renamed.");

    refresh();
    return juce::Result::ok();
}

juce::Result PresetManager::deleteUser (int index)
{
    if (isFactory (index) || ! juce::isPositiveAndBelow (index, getNumPresets()))
        return juce::Result::fail ("Factory presets cannot be deleted.");

    if (! entries[(size_t) index].file.moveToTrash())
        return juce::Result::fail ("The preset could not be deleted.");

    refresh();
    return juce::Result::ok();
}

} // namespace fxtend
