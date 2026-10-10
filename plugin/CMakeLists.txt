cmake_minimum_required(VERSION 3.22)

project(AudioBridgePlugin VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point to JUCE
add_subdirectory("C:/JUCE" JUCE_Build EXCLUDE_FROM_ALL)

juce_add_plugin(AudioBridgev2
    COMPANY_NAME "AudioBridge"
    PLUGIN_MANUFACTURER_CODE AuBr
    PLUGIN_CODE AuB2
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    PLUGIN_NAME "AudioBridge v2"
    PRODUCT_NAME "AudioBridge v2"
    FORMATS VST3
    VST3_CATEGORIES "Fx" "Utility"
)

target_sources(AudioBridgev2
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        ../common/SharedAudioProtocol.h
)

target_include_directories(AudioBridgev2
    PRIVATE
        Source
        ../common
)

target_compile_definitions(AudioBridgev2
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(AudioBridgev2
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
