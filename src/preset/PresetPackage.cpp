#include "PresetPackage.h"

namespace fxtend::PresetPackage
{

namespace
{
    constexpr const char* presetEntry = "preset.json";
    constexpr const char* sampleFolder = "samples/";
    constexpr juce::int64 maxPresetBytes = 4 * 1024 * 1024;
    constexpr juce::int64 maxSampleBytes = 512 * 1024 * 1024;
}

juce::Result exportPackage (const PresetData& preset, const SampleManager& samples, const juce::File& target)
{
    const auto json = PresetSerializer::toJson (preset);

    juce::ZipFile::Builder builder;
    builder.addEntry (std::make_unique<juce::MemoryInputStream> (json.toRawUTF8(), json.getNumBytesAsUTF8(), true),
                      9, presetEntry, juce::Time::getCurrentTime());

    juce::StringArray added;

    for (const auto& ref : preset.sampleRefs)
    {
        if (ref.isEmpty() || added.contains (ref.id))
            continue;

        const auto file = samples.getManagedFile (ref);

        if (! file.existsAsFile())
            return juce::Result::fail ("The sample \"" + ref.name + "\" is missing, so the package would be incomplete. "
                                       "Re-link or clear it first.");

        builder.addFile (file, 0, juce::String (sampleFolder) + file.getFileName());     // audio barely compresses
        added.add (ref.id);
    }

    const auto temporary = target.getSiblingFile (target.getFileNameWithoutExtension() + ".partial" + target.getFileExtension());
    temporary.deleteFile();

    {
        juce::FileOutputStream stream (temporary);

        if (! stream.openedOk())
            return juce::Result::fail ("The package could not be created: " + target.getFullPathName());

        if (! builder.writeToStream (stream, nullptr))
        {
            temporary.deleteFile();
            return juce::Result::fail ("The package could not be written (is the disk full?).");
        }
    }

    if (! temporary.moveFileTo (target))
    {
        temporary.deleteFile();
        return juce::Result::fail ("The package could not be saved to " + target.getFullPathName());
    }

    return juce::Result::ok();
}

juce::Result importPackage (const juce::File& package, const SampleManager& samples, PresetData& preset)
{
    if (! package.existsAsFile())
        return juce::Result::fail ("The package does not exist: " + package.getFullPathName());

    juce::ZipFile zip (package);
    const auto* presetInfo = zip.getEntry (presetEntry);

    if (presetInfo == nullptr || presetInfo->uncompressedSize > maxPresetBytes)
        return juce::Result::fail ("This is not an FXtend preset package.");

    PresetData loaded;

    {
        const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (*presetInfo));

        if (stream == nullptr)
            return juce::Result::fail ("The package could not be read.");

        if (const auto result = PresetSerializer::fromJson (stream->readEntireStreamAsString(), loaded); result.failed())
            return result;
    }

    // Take exactly the sample files the preset refers to, and only if their
    // contents are what the reference says they are.
    for (const auto& ref : loaded.sampleRefs)
    {
        if (ref.isEmpty())
            continue;

        const auto managed = samples.getManagedFile (ref);

        if (managed == juce::File() || managed.getFileName() != ref.fileName || ! SampleManager::hasSupportedExtension (managed))
            return juce::Result::fail ("The package refers to a sample with an invalid name.");

        if (managed.existsAsFile())
            continue;       // already on this computer

        const auto* entry = zip.getEntry (juce::String (sampleFolder) + ref.fileName);

        if (entry == nullptr)
            continue;       // not included: the slot will show as missing

        if (entry->uncompressedSize > maxSampleBytes)
            return juce::Result::fail ("A sample in the package is too large.");

        const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (*entry));

        if (stream == nullptr)
            return juce::Result::fail ("A sample in the package could not be read.");

        samples.getDirectory().createDirectory();
        const auto temporary = samples.getDirectory().getChildFile (ref.fileName + ".importing");
        temporary.deleteFile();

        {
            juce::FileOutputStream output (temporary);

            if (! output.openedOk() || output.writeFromInputStream (*stream, maxSampleBytes) < 0)
            {
                temporary.deleteFile();
                return juce::Result::fail ("A sample could not be copied into " + samples.getDirectory().getFullPathName());
            }
        }

        if (SampleManager::hashFile (temporary) != ref.id)
        {
            temporary.deleteFile();
            return juce::Result::fail ("The sample \"" + ref.name + "\" in the package is damaged or is not the file the preset was made with.");
        }

        if (! temporary.moveFileTo (managed))
        {
            temporary.deleteFile();
            return juce::Result::fail ("A sample could not be stored in " + samples.getDirectory().getFullPathName());
        }
    }

    preset = loaded;
    return juce::Result::ok();
}

} // namespace fxtend::PresetPackage
