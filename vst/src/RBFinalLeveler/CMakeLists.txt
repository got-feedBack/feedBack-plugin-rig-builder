cmake_minimum_required(VERSION 3.22)

project(RBFinalLeveler VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple Silicon. Si quieres universal, cambia a "arm64;x86_64"
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.7
)

FetchContent_MakeAvailable(JUCE)

juce_add_plugin(RBFinalLeveler
    COMPANY_NAME "Rig Builder"
    BUNDLE_ID "com.rigbuilder.finalleveler"
    PLUGIN_MANUFACTURER_CODE Rbld
    PLUGIN_CODE Rbfl
    FORMATS VST3
    PRODUCT_NAME "RB Final Leveler"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(RBFinalLeveler)

target_sources(RBFinalLeveler
    PRIVATE
        Source/PluginProcessor.cpp
)

target_compile_definitions(RBFinalLeveler
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)

target_link_libraries(RBFinalLeveler
    PRIVATE
        juce::juce_audio_processors
)
