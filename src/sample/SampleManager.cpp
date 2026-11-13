#include "SampleManager.h"

#include <juce_cryptography/juce_cryptography.h>

namespace fxtend
{

SampleManager::SampleManager() : SampleManager (getDefaultDirectory()) {}

SampleManager::SampleManager (const juce::File& directoryToUse) : directory (directoryToUse)
{
    formats.registerFormat (new juce::WavAudioFormat(), true);
    formats.registerFormat (new juce::AiffAudioFormat(), false);
}

juce::File SampleManager::getDefaultDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("FXtend")
        .getChildFile ("Samples");
}

bool SampleManager::hasSupportedExtension (const juce::File& file)
{
    return file.hasFileExtension ("wav;wave;aif;aiff");
}

juce::String SampleManager::hashFile (const juce::File& file)
{
    return juce::SHA256 (file).toHexString();
}

juce::Result SampleManager::importFile (const juce::File& source, SampleRef& ref) const
{
    if (! source.existsAsFile())
        return juce::Result::fail ("The file does not exist: " + source.getFullPathName());

    if (! hasSupportedExtension (source))
        return juce::Result::fail ("Only WAV and AIFF files can be loaded.");

    {
        const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));

        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return juce::Result::fail ("\"" + source.getFileName() + "\" could not be read as an audio file.");

        const double seconds = (double) reader->lengthInSamples / reader->sampleRate;

        if (seconds > maxSeconds)
            return juce::Result::fail ("\"" + source.getFileName() + "\" is " + juce::String (seconds, 1)
                                       + " s long. Samples can be at most " + juce::String ((int) maxSeconds) + " s.");
    }

    SampleRef imported;
    imported.id = hashFile (source);
    imported.fileName = imported.id + source.getFileExtension().toLowerCase();
    imported.name = source.getFileName();
    imported.originalPath = source.getFullPathName();

    const auto managed = getManagedFile (imported);

    if (! managed.existsAsFile())
    {
        if (! directory.isDirectory() && directory.createDirectory().failed())
            return juce::Result::fail ("The sample folder could not be created: " + directory.getFullPathName());

        if (! source.copyFileTo (managed))
            return juce::Result::fail ("The sample could not be copied to " + directory.getFullPathName());
    }

    ref = imported;
    return juce::Result::ok();
}

juce::File SampleManager::getManagedFile (const SampleRef& ref) const
{
    // The file name comes from saved state, so never let it point outside the managed folder.
    const auto safeName = juce::File::createLegalFileName (ref.fileName);
    return safeName.isEmpty() ? juce::File() : directory.getChildFile (safeName);
}

bool SampleManager::locate (const SampleRef& ref) const
{
    if (ref.isEmpty())
        return false;

    const auto managed = getManagedFile (ref);

    if (managed.existsAsFile())
        return true;

    // The managed copy is gone (or this is another computer). Take the
    // original only if it is byte-for-byte the file the project was made with.
    const juce::File original (ref.originalPath);

    if (ref.originalPath.isNotEmpty() && juce::File::isAbsolutePath (ref.originalPath)
        && original.existsAsFile() && hashFile (original) == ref.id)
    {
        directory.createDirectory();
        return original.copyFileTo (managed);
    }

    return false;
}

SampleData::Ptr SampleManager::load (const SampleRef& ref, juce::String& error) const
{
    const auto file = getManagedFile (ref);

    if (! file.existsAsFile())
    {
        error = "Missing sample: " + ref.name;
        return {};
    }

    return loadFile (file, error);
}

SampleData::Ptr SampleManager::loadFile (const juce::File& file, juce::String& error) const
{
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "\"" + file.getFileName() + "\" could not be read as an audio file.";
        return {};
    }

    const auto frames = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));

    SampleData::Ptr data (new SampleData());
    data->sampleRate = reader->sampleRate;
    data->audio.setSize (2, frames);

    // Mono files are copied to both channels; anything beyond stereo keeps its first two.
    if (! reader->read (&data->audio, 0, frames, 0, true, true))
    {
        error = "\"" + file.getFileName() + "\" could not be decoded.";
        return {};
    }

    if (reader->numChannels == 1)
        data->audio.copyFrom (1, 0, data->audio, 0, 0, frames);

    return data;
}

} // namespace fxtend
