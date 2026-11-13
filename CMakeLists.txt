cmake_minimum_required(VERSION 3.24)

# Must come before project(): after it, CMake has already fixed the target to
# the macOS version of the build machine and this line is ignored.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(FXtend VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(FXTEND_BUILD_TESTS "Build the engine test suite" ON)
option(FXTEND_COPY_PLUGIN_AFTER_BUILD "Install built plugins into the user plugin folders" ON)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Rubber Band (GPL) does the pitch-preserving time stretch. It has no CMake
# build of its own; its single-file build is all that is needed here.
FetchContent_Declare(rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG v4.0.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(rubberband)

add_library(rubberband STATIC ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp)
target_include_directories(rubberband SYSTEM PUBLIC ${rubberband_SOURCE_DIR})
target_compile_options(rubberband PRIVATE -w)
set_target_properties(rubberband PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Dependencies")

if(APPLE)
    target_link_libraries(rubberband PUBLIC "-framework Accelerate")
endif()

# ---------------------------------------------------------------------------
# Engine sources: host/UI independent. Compiled into both the plugin and the
# test runner so the same code is exercised in both.
# ---------------------------------------------------------------------------
set(FXTEND_ENGINE_SOURCES
    src/engine/TransportClock.cpp
    src/engine/FxVoice.cpp
    src/engine/SynthLayer.cpp
    src/engine/SampleLayer.cpp
    src/engine/FxTemplates.cpp
    src/engine/LayerFields.cpp
    src/engine/OfflineRenderer.cpp
    src/engine/MasterFx.cpp
    src/engine/WavExport.cpp
    src/dsp/TimeStretchProcessor.cpp
    src/sample/SampleManager.cpp
    src/preset/PresetSerializer.cpp
    src/preset/FactoryPresets.cpp
    src/preset/PresetManager.cpp
    src/preset/PresetPackage.cpp
    src/engine/FxEngine.cpp)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(FXtend
    PRODUCT_NAME "FXtend"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/logo/fxtend-icon-1024.png"
    COMPANY_NAME "CHEEBOW"
    BUNDLE_ID "info.cheebow.fxtend"
    PLUGIN_MANUFACTURER_CODE Chbw
    PLUGIN_CODE Fxtd
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD ${FXTEND_COPY_PLUGIN_AFTER_BUILD})

target_sources(FXtend PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Parameters.cpp
    src/ui/Theme.cpp
    src/ui/WaveformView.cpp
    src/ui/LayerEditor.cpp
    src/ui/FieldControl.cpp
    src/ui/SamplePanel.cpp
    ${FXTEND_ENGINE_SOURCES})

target_include_directories(FXtend PRIVATE src)

target_compile_definitions(FXtend PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(FXtend
    PRIVATE
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
        rubberband
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FXTEND_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    # The tests link the plugin's shared code, so they exercise exactly what
    # ships: the engine on its own and the AudioProcessor/editor on top of it.
    add_executable(FXtendTests
        tests/TimingTests.cpp
        tests/SynthTests.cpp
        tests/PresetTests.cpp
        tests/TailTests.cpp
        tests/SampleTests.cpp
        tests/SampleFileTests.cpp
        tests/ExportTests.cpp
        tests/AudioRegressionTests.cpp
        tests/RealtimeTests.cpp
        tests/PluginTests.cpp)

    target_include_directories(FXtendTests PRIVATE
        src
        $<TARGET_PROPERTY:FXtend,INCLUDE_DIRECTORIES>)

    target_compile_definitions(FXtendTests PRIVATE
        $<TARGET_PROPERTY:FXtend,COMPILE_DEFINITIONS>)

    target_link_libraries(FXtendTests PRIVATE
        FXtend
        rubberband
        Catch2::Catch2WithMain)

    enable_testing()
    add_test(NAME FXtendTests COMMAND FXtendTests)
endif()
