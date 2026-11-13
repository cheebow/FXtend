#pragma once

#include <juce_core/juce_core.h>

namespace fxtend
{

/** Identifies the audio file used by a sample slot.

    The plugin never stores the audio itself in a project or preset; it stores
    this reference. `id` is the SHA-256 of the file contents, and the managed
    copy lives in the sample folder under `fileName`. */
struct SampleRef
{
    juce::String id;                // content hash; empty = no sample
    juce::String fileName;          // name of the managed copy, "<id>.<extension>"
    juce::String name;              // what the user sees (the original file name)
    juce::String originalPath;      // where it was loaded from, for information and re-linking

    bool isEmpty() const noexcept                               { return id.isEmpty(); }
    bool operator== (const SampleRef& other) const noexcept     { return id == other.id; }
    bool operator!= (const SampleRef& other) const noexcept     { return id != other.id; }
};

} // namespace fxtend
