#pragma once

#include "engine/FxEngine.h"
#include "engine/LayerFields.h"
#include "preset/PresetSerializer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

/** Host parameter ids. These are saved in DAW projects and automation:
    never rename them. */
namespace ParamIDs
{
    inline constexpr const char* fxType = "fxType";
    inline constexpr const char* lengthUnit = "lengthUnit";
    inline constexpr const char* lengthValue = "lengthValue";
    inline constexpr const char* tailMode = "tailMode";
    inline constexpr const char* retrigger = "retrigger";
    inline constexpr const char* manualBpm = "manualBpm";
    inline constexpr const char* masterGain = "masterGain";

    juce::String macro (int index);                                             // macro1..macro4
    juce::String synthLayer (int layer, const fxtend::LayerField& field);      // s1_level ...
    juce::String sampleLayer (int slot, const fxtend::SampleField& field);      // p1_mode ...
    juce::String master (const fxtend::MasterField& field);                    // m_reverbMix ...
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Reads the current parameter values into engine structs without locking;
    safe on the audio thread. */
class ParameterReader
{
public:
    explicit ParameterReader (juce::AudioProcessorValueTreeState& state);

    /** Fills everything that is a host parameter. Custom curve points in the
        patch are left untouched. */
    void read (fxtend::Patch& patch, fxtend::EngineSettings& settings) const noexcept;

private:
    std::atomic<float>* fxType = nullptr;
    std::atomic<float>* lengthUnit = nullptr;
    std::atomic<float>* lengthValue = nullptr;
    std::atomic<float>* tailMode = nullptr;
    std::atomic<float>* retrigger = nullptr;
    std::atomic<float>* manualBpm = nullptr;
    std::atomic<float>* masterGain = nullptr;
    std::array<std::atomic<float>*, fxtend::numMacros> macros {};
    std::array<std::vector<std::atomic<float>*>, fxtend::numSynthLayers> layers;
    std::array<std::vector<std::atomic<float>*>, fxtend::numSampleLayers> sampleValues;
    std::vector<std::atomic<float>*> masterValues;
};

/** Message thread: pushes a patch into the host parameters (notifying the host). */
void applyPatchToParameters (juce::AudioProcessorValueTreeState& state, const fxtend::Patch& patch);
