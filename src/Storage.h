#pragma once

#include <juce_core/juce_core.h>

namespace fxtend
{

/** The folder FXtend keeps its user data in (presets and managed samples). */
inline juce::File getAppDataDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("FXtend");
}

} // namespace fxtend
