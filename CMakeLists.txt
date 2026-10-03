cmake_minimum_required(VERSION 3.22)
project(eCU10MST VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(eCU10MST
    COMPANY_NAME "Pablo Olivares"
    PLUGIN_MANUFACTURER_CODE Polv
    PLUGIN_CODE Ecm1
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "eCU-10 MST"
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(eCU10MST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Widgets.cpp
    Source/Presets.cpp)

target_compile_definitions(eCU10MST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(eCU10MST
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Herramienta de desarrollo: dibuja el editor a un PNG (solo se compila si se pide el objetivo EQScreenshot).
juce_add_console_app(EQScreenshot PRODUCT_NAME "EQScreenshot")
target_sources(EQScreenshot PRIVATE
    Tools/Screenshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Widgets.cpp
    Source/Presets.cpp)
target_include_directories(EQScreenshot PRIVATE Source)
target_compile_definitions(EQScreenshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(EQScreenshot
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

# Prueba de latencia: mide con un impulso que la latencia informada es la real (se ejecuta en CI).
juce_add_console_app(EQLatencyTest PRODUCT_NAME "EQLatencyTest")
target_sources(EQLatencyTest PRIVATE
    Tools/LatencyTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Widgets.cpp
    Source/Presets.cpp)
target_include_directories(EQLatencyTest PRIVATE Source)
target_compile_definitions(EQLatencyTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(EQLatencyTest
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

# Pruebas funcionales: respuesta de cada modo de fase, canales, dinámica, medición, ranuras (se ejecutan en CI).
juce_add_console_app(EQFeatureTest PRODUCT_NAME "EQFeatureTest")
target_sources(EQFeatureTest PRIVATE
    Tools/FeatureTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Widgets.cpp
    Source/Presets.cpp)
target_include_directories(EQFeatureTest PRIVATE Source)
target_compile_definitions(EQFeatureTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(EQFeatureTest
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
