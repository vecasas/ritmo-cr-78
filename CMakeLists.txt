cmake_minimum_required(VERSION 3.22)
project(RitmoCR78 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
# Universal binary (Intel + Apple Silicon) on macOS
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(R78_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND R78_FORMATS AU)
endif()

juce_add_plugin(RitmoCR78
    COMPANY_NAME "Victor"
    BUNDLE_ID "com.victor.ritmocr78"
    PLUGIN_MANUFACTURER_CODE Vict
    PLUGIN_CODE Rt78
    FORMATS ${R78_FORMATS}
    PRODUCT_NAME "Ritmo CR-78"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(RitmoCR78)

target_sources(RitmoCR78 PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/DspUtils.h
    src/dsp/Voices.h
    src/seq/Sequencer.h
    src/seq/FactoryRhythms.cpp
    src/StepGrid.h)

target_compile_definitions(RitmoCR78 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(RitmoCR78
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Herramienta de línea de comandos que renderiza las voces a WAV (sin JUCE).
add_executable(render_demo tools/render_demo.cpp)
target_compile_features(render_demo PRIVATE cxx_std_20)
target_sources(render_demo PRIVATE src/seq/FactoryRhythms.cpp)

# Pruebas del secuenciador (sin JUCE): ctest --test-dir build -C Release
enable_testing()
add_executable(test_sequencer tests/test_sequencer.cpp src/seq/FactoryRhythms.cpp)
target_compile_features(test_sequencer PRIVATE cxx_std_20)
add_test(NAME sequencer COMMAND test_sequencer)
