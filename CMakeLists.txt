cmake_minimum_required(VERSION 3.22)

project(Melomanix VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.14
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(JUCE)

# Copies the built VST3 into the user's plugin folder for local iteration;
# disabled in CI where the Windows destination needs admin rights.
option(MELOMANIX_COPY_PLUGIN "Copy plugin to user plugin dir after build" ON)

juce_add_plugin(Melomanix
    COMPANY_NAME "Melomanix"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD ${MELOMANIX_COPY_PLUGIN}
    PLUGIN_MANUFACTURER_CODE Mlmx
    PLUGIN_CODE Mlm0
    FORMATS VST3 Standalone
    PRODUCT_NAME "Melomanix")

target_sources(Melomanix PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/engine/GraphTypes.cpp
    src/engine/Nodes.cpp
    src/engine/GraphEngine.cpp
    src/model/GraphModel.cpp)

target_compile_definitions(Melomanix PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Melomanix
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_console_app(MelomanixTests PRODUCT_NAME "MelomanixTests")

target_sources(MelomanixTests PRIVATE
    test/EngineTests.cpp
    src/engine/GraphTypes.cpp
    src/engine/Nodes.cpp
    src/engine/GraphEngine.cpp
    src/model/GraphModel.cpp)

target_link_libraries(MelomanixTests
    PRIVATE
        juce::juce_data_structures
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
