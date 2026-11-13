#pragma once

#include "PresetSerializer.h"

#include <vector>

namespace fxtend
{

/** Lists factory and user presets and reads/writes the user ones on disk.
    Use from the message thread only. */
class PresetManager
{
public:
    /** Uses the standard user preset folder. */
    PresetManager();

    /** Uses the given folder for user presets (tests). */
    explicit PresetManager (const juce::File& userDirectory);

    static juce::File getDefaultUserDirectory();

    void refresh();

    int getNumPresets() const noexcept                  { return (int) entries.size(); }
    int getNumFactoryPresets() const noexcept           { return numFactory; }
    juce::String getName (int index) const;
    bool isFactory (int index) const noexcept           { return index >= 0 && index < numFactory; }
    FxType getFxType (int index) const;
    int indexOf (const juce::String& name) const;

    juce::Result load (int index, PresetData& preset) const;

    /** Saves (or overwrites) a user preset under the given name. */
    juce::Result saveUser (const juce::String& name, const PresetData& preset);
    juce::Result renameUser (int index, const juce::String& newName);
    juce::Result deleteUser (int index);

    const juce::File& getUserDirectory() const noexcept { return userDirectory; }

private:
    struct Entry
    {
        juce::String name;
        FxType fxType = FxType::riser;
        juce::File file;        // empty for factory presets
    };

    juce::File fileFor (const juce::String& name) const;
    juce::Result checkName (const juce::String& name) const;

    juce::File userDirectory;
    std::vector<Entry> entries;
    int numFactory = 0;
};

} // namespace fxtend
