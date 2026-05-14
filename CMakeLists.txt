cmake_minimum_required(VERSION 3.22)

project(hyperframe_core VERSION 0.1.0 LANGUAGES C CXX)

option(BUILD_JUCE_PLUGIN "Build the JUCE VST3 plugin wrapper" OFF)
set(HYPERFRAME_COMPANY_NAME "HyperFrame" CACHE STRING "Company/vendor name embedded in plugin metadata")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hyperframe_core
    src/dsp/AdsrEnvelope.cpp
    src/dsp/Bytebeat.cpp
    src/dsp/SynthEngine.cpp
    src/dsp/Voice.cpp
    src/dsp/WaveTable.cpp
    src/dsp/WaveTableOscillator.cpp
)

target_include_directories(hyperframe_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(core_smoke_test
    tools/core_smoke_test.cpp
)

target_link_libraries(core_smoke_test
    PRIVATE hyperframe_core
)

if(BUILD_JUCE_PLUGIN)
    include(FetchContent)

    FetchContent_Declare(
        liblsdj
        GIT_REPOSITORY https://github.com/stijnfrishert/liblsdj.git
        GIT_TAG 6023c4e48ad8280abacfddba60f2689e2442d79c
    )
    FetchContent_GetProperties(liblsdj)
    if(NOT liblsdj_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(liblsdj)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
        if(NOT TARGET Catch2::Catch2)
            add_library(Catch2::Catch2 INTERFACE IMPORTED)
        endif()
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        set(HYPERFRAME_PREVIOUS_WARN_DEPRECATED "$CACHE{CMAKE_WARN_DEPRECATED}")
        set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Suppress third-party CMake deprecation warnings while adding liblsdj" FORCE)
        add_subdirectory(${liblsdj_SOURCE_DIR}/liblsdj ${liblsdj_BINARY_DIR} EXCLUDE_FROM_ALL)
        if(HYPERFRAME_PREVIOUS_WARN_DEPRECATED STREQUAL "")
            unset(CMAKE_WARN_DEPRECATED CACHE)
        else()
            set(CMAKE_WARN_DEPRECATED ${HYPERFRAME_PREVIOUS_WARN_DEPRECATED} CACHE BOOL "Whether to issue warnings for deprecated functionality" FORCE)
        endif()
        if(MSVC)
            target_compile_options(liblsdj PRIVATE /wd4098)
        endif()
    endif()

    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(HyperFrame
        COMPANY_NAME "${HYPERFRAME_COMPANY_NAME}"
        VERSION "${PROJECT_VERSION}"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE HFrm
        PLUGIN_CODE HFrM
        FORMATS VST3
        PRODUCT_NAME "HyperFrame"
    )

    target_sources(HyperFrame
        PRIVATE
            plugin/PluginEditor.cpp
            plugin/PluginProcessor.cpp
    )

    target_link_libraries(HyperFrame
        PRIVATE
            hyperframe_core
            liblsdj
            juce::juce_audio_formats
            juce::juce_audio_utils
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    target_compile_definitions(HyperFrame
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
    )

    if(MSVC)
        target_compile_options(HyperFrame PRIVATE /utf-8)
    endif()
endif()
