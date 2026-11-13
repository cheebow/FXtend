#include "WavExport.h"
#include "../dsp/TimeStretchProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace fxtend
{

bool prepareStretchedSamples (RenderRequest& request, const std::function<bool()>& shouldAbort)
{
    const double bodySeconds = FxEngine::lengthInQuarterNotes (request.settings.lengthUnit, request.settings.lengthValue, request.timeSig)
                               * 60.0 / std::max (1.0, request.bpm);

    for (size_t slot = 0; slot < request.samples.size(); ++slot)
    {
        const auto job = makeStretchJob (request.samples[slot], request.patch.samples[slot], bodySeconds);

        if (! job.isValid())
        {
            request.stretched[slot] = nullptr;
            continue;
        }

        request.stretched[slot] = stretchSample (job, shouldAbort);

        if (shouldAbort && shouldAbort())
            return false;
    }

    return true;
}

juce::Result exportWav (RenderRequest request, const juce::File& target, const WavExportOptions& options,
                        const std::function<bool()>& shouldAbort)
{
    request.sampleRate = options.sampleRate;
    request.velocity = (float) juce::jlimit (1, 127, options.velocity) / 127.0f;

    if (! prepareStretchedSamples (request, shouldAbort))
        return juce::Result::fail ("The export was cancelled.");

    const auto rendered = renderOffline (request, shouldAbort);

    if (! rendered.completed)
        return juce::Result::fail ("The export was cancelled.");

    if (rendered.audio.getNumSamples() <= 0)
        return juce::Result::fail ("There is nothing to export.");

    // Write next to the target first, so a failed export never leaves a broken file in its place.
    const auto temporary = target.getSiblingFile (target.getFileNameWithoutExtension() + ".partial" + target.getFileExtension());
    temporary.deleteFile();

    {
        std::unique_ptr<juce::OutputStream> stream = temporary.createOutputStream();

        if (stream == nullptr)
            return juce::Result::fail ("The file could not be created: " + target.getFullPathName());

        juce::WavAudioFormat format;
        auto writerOptions = juce::AudioFormatWriterOptions().withSampleRate (options.sampleRate)
                                                             .withNumChannels (2)
                                                             .withBitsPerSample (options.floatingPoint ? 32 : 24);

        if (options.floatingPoint)
            writerOptions = writerOptions.withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        auto writer = format.createWriterFor (stream, writerOptions);

        if (writer == nullptr)
        {
            temporary.deleteFile();
            return juce::Result::fail ("This WAV format is not supported.");
        }

        if (! writer->writeFromAudioSampleBuffer (rendered.audio, 0, rendered.audio.getNumSamples()))
        {
            writer.reset();
            temporary.deleteFile();
            return juce::Result::fail ("The audio could not be written (is the disk full?).");
        }
    }

    if (! temporary.moveFileTo (target))
    {
        temporary.deleteFile();
        return juce::Result::fail ("The file could not be saved to " + target.getFullPathName());
    }

    return juce::Result::ok();
}

} // namespace fxtend
