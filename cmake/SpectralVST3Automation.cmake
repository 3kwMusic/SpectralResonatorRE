# SPDX-FileCopyrightText: 2026 TriM
# SPDX-License-Identifier: AGPL-3.0-only

# Generate a project-local JUCE 8.0.4 VST3 adapter. The external JUCE checkout is
# never edited. A source digest prevents silently applying this patch to a newer
# wrapper with changed contracts. Reassess and update the patch when JUCE changes.
function(spectral_enable_vst3_automation plugin_target)
    set(upstream "${JUCE_DIR}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
    file(SHA256 "${upstream}" actual_sha)
    set(expected_sha "76f01b80b84146d620caa5bd97bb411a63cc9f765310c7e92d44935ce4e64ff4")
    if(NOT actual_sha STREQUAL expected_sha)
        message(FATAL_ERROR "Spectral VST3 automation adapter requires verified JUCE 8.0.4 wrapper ${expected_sha}; found ${actual_sha}")
    endif()

    file(READ "${upstream}" wrapper)
    string(REPLACE "\r\n" "\n" wrapper "${wrapper}")
    file(READ "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SpectralVST3Automation.inl" bridge)
    set(include_anchor "#include <juce_audio_processors/format_types/juce_VST3Common.h>")
    string(REPLACE "${include_anchor}" "${include_anchor}\n#include \"${PROJECT_SOURCE_DIR}/Source/AutomationBridge.h\"" wrapper "${wrapper}")

    # Only MIDI emulation queues are consumed before audio. Ordinary parameters,
    # including JUCE's synthetic bypass parameter, are deferred to their offset.
    string(REPLACE
        "void processParameterChanges (Vst::IParameterChanges& paramChanges)"
        "void processParameterChanges (Vst::IParameterChanges& paramChanges, bool deferOrdinaryParameters = false)"
        wrapper "${wrapper}")
    set(point_reader_original [=[            return queue->getPoint (index, result.offsetSamples, result.value) == kResultTrue
                   ? makeOptional (result)
                   : nullopt;]=])
    set(point_reader_safe [=[            if (queue->getPoint (index, result.offsetSamples, result.value) != kResultTrue
                || ! std::isfinite (result.value))
                return Optional<ParamChangeInfo> {};
            result.value = jlimit (0.0, 1.0, result.value);
            return makeOptional (result);]=])
    string(REPLACE "${point_reader_original}" "${point_reader_safe}" wrapper "${wrapper}")
    set(last_finite_reader [=[        const auto getLastFinitePointFromQueue = [&] (Vst::IParamValueQueue* queue, Steinberg::int32 count) -> Optional<ParamChangeInfo>
        {
            for (auto index = jmax ((Steinberg::int32) 0, count); index > 0; --index)
                if (const auto value = getPointFromQueue (queue, index - 1))
                    return value;
            return nullopt;
        };

        const auto numParamsChanged = paramChanges.getParameterCount();]=])
    string(REPLACE "        const auto numParamsChanged = paramChanges.getParameterCount();" "${last_finite_reader}" wrapper "${wrapper}")
    string(REPLACE
        "if (const auto change = getPointFromQueue (paramQueue, numPoints - 1))"
        "if (! deferOrdinaryParameters) if (const auto change = getLastFinitePointFromQueue (paramQueue, numPoints))"
        wrapper "${wrapper}")
    set(parameter_dispatch [=[{
            auto* automation = dynamic_cast<SpectralAutomationConsumer*> (pluginInstance);
            const auto defer = automation != nullptr && ! pluginInstance->isSuspended()
                && (data.numSamples != 0 || data.numInputs != 0 || data.numOutputs != 0);
            processParameterChanges (*data.inputParameterChanges, defer);
            if (automation != nullptr && ! defer)
            {
                SpectralVST3AutomationSource source (*this, data.inputParameterChanges);
                automation->synchronizeAutomationFlush (source);
            }
        }]=])
    string(REPLACE
        "processParameterChanges (*data.inputParameterChanges);"
        "${parameter_dispatch}"
        wrapper "${wrapper}")

    # The SDK queue and wrapper ID mapping live only in this adapter; processor
    # receives an ephemeral interface and handles each merged tuple as one update.
    set(audio_anchor "    template <typename FloatType>\n    void processAudio (Vst::ProcessData& data)")
    string(REPLACE "${audio_anchor}" "${bridge}\n${audio_anchor}" wrapper "${wrapper}")
    set(dispatch_anchor "                if (pluginInstance->getBypassParameter() == nullptr && comPluginInstance->getBypassParameter()->getValue() >= 0.5f)")
    set(dispatch_replacement [=[                if (auto* automation = dynamic_cast<SpectralAutomationConsumer*> (pluginInstance))
                {
                    if constexpr (std::is_same_v<FloatType, float>)
                    {
                        SpectralVST3AutomationSource source (*this, data.inputParameterChanges);
                        automation->processBlockWithAutomation (buffer, midiBuffer, source);
                    }
                    else
                    {
                        jassertfalse; // This processor does not advertise double precision.
                        buffer.clear();
                    }
                }
                else if (pluginInstance->getBypassParameter() == nullptr && comPluginInstance->getBypassParameter()->getValue() >= 0.5f)]=])
    string(REPLACE "${dispatch_anchor}" "${dispatch_replacement}" wrapper "${wrapper}")

    foreach(required "class SpectralVST3AutomationSource final" "bool deferOrdinaryParameters = false" "getLastFinitePointFromQueue (paramQueue, numPoints)" "|| ! std::isfinite (result.value)" "automation->processBlockWithAutomation (buffer, midiBuffer, source)" "automation->synchronizeAutomationFlush (source)")
        string(FIND "${wrapper}" "${required}" required_offset)
        if(required_offset LESS 0)
            message(FATAL_ERROR "Spectral VST3 adapter anchor failed: ${required}")
        endif()
    endforeach()

    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/spectral-vst3-adapter")
    file(MAKE_DIRECTORY "${output_dir}")
    set(output "${output_dir}/juce_audio_plugin_client_VST3.cpp")
    # configure_file(COPYONLY) semantics without rewriting an unchanged output.
    set(write_output TRUE)
    if(EXISTS "${output}")
        file(READ "${output}" existing)
        if(existing STREQUAL wrapper)
            set(write_output FALSE)
        endif()
    endif()
    if(write_output)
        file(WRITE "${output}" "${wrapper}")
    endif()
    file(SHA256 "${output}" adapter_sha)
    file(WRITE "${output_dir}/identity.txt" "upstream=${upstream}\nupstream_sha256=${actual_sha}\nadapter_sha256=${adapter_sha}\n")

    # Disable only this source in the current project's target directory. Other
    # JUCE wrappers remain untouched; this project has one VST3 plugin target.
    set_source_files_properties("${upstream}" TARGET_DIRECTORY ${plugin_target}_VST3 PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(${plugin_target}_VST3 PRIVATE "${output}")
endfunction()
