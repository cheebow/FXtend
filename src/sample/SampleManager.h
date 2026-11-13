#pragma once

#include "SampleRef.h"
#include "../engine/SampleData.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace fxtend
{

/** Looks after the user's sample files.

    Importing a file copies it into a managed folder under a name derived from
    its contents, so a project keeps working after the original file has been
    moved or renamed. None of this may be called from the audio thread. */
class SampleManager
{
public:
    static constexpr double maxSeconds = 60.0;

    /** Uses the standard managed sample folder. */
    SampleManager();

    /** Uses the given folder (tests). */
    explicit SampleManager (const juce::File& directory);

    static juce::File getDefaultDirectory();
    const juce::File& getDirectory() const noexcept { return directory; }

    /** Checks the file (WAV or AIFF, at most maxSeconds long), copies it into
        the managed folder if it is not there yet, and fills in the reference. */
    juce::Result importFile (const juce::File& source, SampleRef& ref) const;

    /** The managed copy for a reference (which may not exist on this computer). */
    juce::File getManagedFile (const SampleRef& ref) const;

    /** Makes sure the managed copy exists, restoring it from the original
        location if that file is still there and still has the same contents.
        Returns false if the sample cannot be found: the caller should then
        report it as missing rather than substitute anything. */
    bool locate (const SampleRef& ref) const;

    /** Decodes the managed copy into memory (two channels, original sample rate). */
    SampleData::Ptr load (const SampleRef& ref, juce::String& error) const;

    /** Decodes any supported audio file. */
    SampleData::Ptr loadFile (const juce::File& file, juce::String& error) const;

    static juce::String hashFile (const juce::File& file);
    static bool hasSupportedExtension (const juce::File& file);

private:
    juce::File directory;
    mutable juce::AudioFormatManager formats;
};

} // namespace fxtend
