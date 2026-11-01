# SPDX-FileCopyrightText: 2026 TriM
# SPDX-License-Identifier: AGPL-3.0-only

cmake_minimum_required(VERSION 3.22)
project(SpectralResonatorRE VERSION 1.5.0)

include(cmake/JuceDependency.cmake)
spectral_add_juce()

juce_add_plugin(SpectralResonatorRE
    PRODUCT_NAME "Spectral Resonator RE"
    PLUGIN_MANUFACTURER_CODE ReAr
    PLUGIN_CODE Spr1
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Filter)

target_sources(SpectralResonatorRE PRIVATE
    Source/Parameters.cpp
    Source/SpectralEngine.cpp
    Source/PluginProcessor.cpp
    Source/ModernLookAndFeel.cpp
    Source/PluginEditor.cpp)

target_compile_features(SpectralResonatorRE PRIVATE cxx_std_17)
target_compile_definitions(SpectralResonatorRE PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(SpectralResonatorRE PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

include(cmake/SpectralVST3Automation.cmake)
spectral_enable_vst3_automation(SpectralResonatorRE)
