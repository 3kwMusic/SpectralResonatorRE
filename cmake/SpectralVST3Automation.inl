// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

    class SpectralVST3AutomationSource final : public SpectralAutomationSource
    {
    public:
        SpectralVST3AutomationSource (JuceVST3Component& componentIn, Vst::IParameterChanges* changesIn) noexcept
            : component (componentIn), changes (changesIn) {}

        int queueCount() const noexcept override
        {
            return changes != nullptr ? jmax (0, (int) changes->getParameterCount()) : 0;
        }

        int pointCount (int queueIndex) const noexcept override
        {
            if (auto* queue = getQueue (queueIndex))
                if (getParameter (queue) != nullptr)
                    return jmax (0, (int) queue->getPointCount());
            return 0;
        }

        bool getPoint (int queueIndex, int pointIndex, int& offset, double& normalized) const noexcept override
        {
            if (auto* queue = getQueue (queueIndex))
            {
                if (getParameter (queue) == nullptr || pointIndex < 0 || pointIndex >= queue->getPointCount())
                    return false;
                Steinberg::int32 sdkOffset = 0;
                Vst::ParamValue sdkValue = 0;
                if (queue->getPoint (pointIndex, sdkOffset, sdkValue) != kResultTrue)
                    return false;
                offset = (int) sdkOffset;
                normalized = sdkValue;
                return true;
            }
            return false;
        }

        void applyPoint (int queueIndex, double normalized) noexcept override
        {
            if (! std::isfinite (normalized))
                return;
            if (auto* queue = getQueue (queueIndex))
                if (auto* parameter = getParameter (queue))
                    setValueAndNotifyIfChanged (*parameter, (float) jlimit (0.0, 1.0, normalized));
        }

        double currentNormalized (int queueIndex) const noexcept override
        {
            if (auto* queue = getQueue (queueIndex))
                if (auto* parameter = getParameter (queue))
                    return parameter->getValue();
            return 0.0;
        }

        bool isDiscrete (int queueIndex) const noexcept override
        {
            if (auto* queue = getQueue (queueIndex))
                if (auto* parameter = getParameter (queue))
                    return parameter == component.comPluginInstance->getBypassParameter() || parameter->isDiscrete();
            return true;
        }

        int parameterIndex (int queueIndex) const noexcept override
        {
            if (auto* queue = getQueue (queueIndex))
                if (auto* parameter = getParameter (queue))
                    return parameter == component.comPluginInstance->getBypassParameter()
                        ? component.pluginInstance->getParameters().size() : parameter->getParameterIndex();
            return -1;
        }

        bool isBypassed() const noexcept override
        {
            if (auto* parameter = component.comPluginInstance->getBypassParameter())
                return parameter->getValue() >= 0.5f;
            return false;
        }

    private:
        Vst::IParamValueQueue* getQueue (int index) const noexcept
        {
            if (changes == nullptr || index < 0 || index >= changes->getParameterCount())
                return nullptr;
            return changes->getParameterData (index);
        }

        AudioProcessorParameter* getParameter (Vst::IParamValueQueue* queue) const noexcept
        {
           #if JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS
            if (component.juceVST3EditController != nullptr
                && component.juceVST3EditController->isMidiControllerParamID (queue->getParameterId()))
                return nullptr;
           #endif
            return component.comPluginInstance->getParamForVSTParamID (queue->getParameterId());
        }

        JuceVST3Component& component;
        Vst::IParameterChanges* changes = nullptr;
    };
