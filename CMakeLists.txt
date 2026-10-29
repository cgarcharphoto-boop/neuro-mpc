cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(NEURO_MPC VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pinned: JUCE 9.0.4 (3fb5cbc0340758596dac39f7c8de47d562b4896e). See Scripts/fetch-juce.sh.
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Run Scripts/fetch-juce.sh or pass -DJUCE_PATH=...")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

# Vendored, MIT: Signalsmith Linear 0.6.4 (de55e6a), Signalsmith Stretch 1.4.0 (a670068). See Documentation/LICENSES.md.
add_subdirectory(ThirdParty/signalsmith-linear SYSTEM)
add_subdirectory(ThirdParty/signalsmith-stretch SYSTEM)

juce_add_plugin(NEURO_MPC
    COMPANY_NAME "NEURO MPC"
    BUNDLE_ID "org.neurompc.neurompc"
    PLUGIN_MANUFACTURER_CODE Nrmp
    PLUGIN_CODE Nm26
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "NEURO MPC"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(NEURO_MPC PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Audio/VoiceEngine.cpp
    Source/DSP/TimeStretch.cpp
    Source/UI/WaveformView.cpp)

target_include_directories(NEURO_MPC PRIVATE Source)

target_compile_definitions(NEURO_MPC PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(NEURO_MPC
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        signalsmith-stretch
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# JUCE-free unit tests for the core model and DSP helpers.
enable_testing()
add_executable(neuro_core_tests Tests/CoreTests.cpp)
target_include_directories(neuro_core_tests PRIVATE Source)
target_compile_options(neuro_core_tests PRIVATE -Wall -Wextra)
add_test(NAME core COMMAND neuro_core_tests)

# Headless integration tests against the real processor (same sources, no plugin wrapper).
juce_add_console_app(neuro_engine_tests PRODUCT_NAME "neuro_engine_tests")
target_sources(neuro_engine_tests PRIVATE
    Tests/EngineTests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Audio/VoiceEngine.cpp
    Source/DSP/TimeStretch.cpp
    Source/UI/WaveformView.cpp)
target_include_directories(neuro_engine_tests PRIVATE Source)
target_compile_definitions(neuro_engine_tests PRIVATE
    JucePlugin_Name="NEURO MPC"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(neuro_engine_tests PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    signalsmith-stretch
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME engine COMMAND neuro_engine_tests)
