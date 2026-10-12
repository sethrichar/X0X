cmake_minimum_required(VERSION 3.22)
project(X0X VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --- JUCE -------------------------------------------------------------------
# Fetched automatically. To use a local checkout instead:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Plugin -----------------------------------------------------------------
set(X0X_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND X0X_FORMATS AU)
endif()

juce_add_plugin(X0X
    COMPANY_NAME               "Richardson Media Solutions"
    PRODUCT_NAME               "X0X"
    PLUGIN_MANUFACTURER_CODE   Rmsx
    PLUGIN_CODE                X0x8
    FORMATS                    ${X0X_FORMATS}
    IS_SYNTH                   TRUE
    NEEDS_MIDI_INPUT           TRUE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD    FALSE
    VST3_CATEGORIES            Instrument Drum
    AU_MAIN_TYPE               kAudioUnitType_MusicDevice
    BUNDLE_ID                  com.richardsonmedia.x0x)

juce_generate_juce_header(X0X)

target_sources(X0X PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DrumSynth.cpp
    Source/Neve1073.cpp)

target_include_directories(X0X PRIVATE Source)

target_compile_definitions(X0X PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(X0X PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# --- Offline render / sanity test -------------------------------------------
option(X0X_BUILD_TESTS "Build the offline render test" ON)
if(X0X_BUILD_TESTS)
    juce_add_console_app(x0x_render PRODUCT_NAME "x0x_render")
    juce_generate_juce_header(x0x_render)
    target_sources(x0x_render PRIVATE
        tests/RenderTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DrumSynth.cpp
        Source/Neve1073.cpp)
    target_include_directories(x0x_render PRIVATE Source)
    target_compile_definitions(x0x_render PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JucePlugin_Name="X0X"
        JucePlugin_Build_Standalone=1
        JucePlugin_IsSynth=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=1
        JucePlugin_ManufacturerCode=0x526d7378
        JucePlugin_PluginCode=0x58307838
        JucePlugin_Manufacturer="Richardson Media Solutions"
        JucePlugin_VersionString="0.1.0"
        JucePlugin_Version=0.1.0
        JucePlugin_VersionCode=0x100)
    target_link_libraries(x0x_render PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

