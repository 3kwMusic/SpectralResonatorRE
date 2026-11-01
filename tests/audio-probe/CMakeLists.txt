# SPDX-FileCopyrightText: 2026 TriM
# SPDX-License-Identifier: AGPL-3.0-only

cmake_minimum_required(VERSION 3.22)
project(SpectralAudioProbe VERSION 1.0.0)
include(../../cmake/JuceDependency.cmake)
spectral_add_juce()
juce_add_console_app(SpectralAudioProbe)
target_sources(SpectralAudioProbe PRIVATE
    AudioProbe.cpp
    ../../Source/Parameters.cpp
    ../../Source/SpectralEngine.cpp
    ../../Source/PluginProcessor.cpp
    ../../Source/ModernLookAndFeel.cpp
    ../../Source/PluginEditor.cpp)
target_compile_features(SpectralAudioProbe PRIVATE cxx_std_17)
target_compile_definitions(SpectralAudioProbe PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Spectral Resonator RE")
target_link_libraries(SpectralAudioProbe PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

enable_testing()
get_filename_component(source_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(SPECTRAL_TEST_VST3 "${source_root}/build/SpectralResonatorRE_artefacts/Release/VST3/Spectral Resonator RE.vst3" CACHE PATH "VST3 bundle to validate")
add_test(NAME audio-probe COMMAND SpectralAudioProbe)
set_tests_properties(audio-probe PROPERTIES WORKING_DIRECTORY "${source_root}" ENVIRONMENT "SPECTRAL_TEST_VST3=${SPECTRAL_TEST_VST3}")
