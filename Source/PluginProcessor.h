// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "SpectralEngine.h"
#include "Parameters.h"
#include "AutomationBridge.h"

class SpectralResonatorProcessor final : public juce::AudioProcessor,
                                        public SpectralAutomationConsumer
{
public:
    SpectralResonatorProcessor();
    ~SpectralResonatorProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockWithAutomation(juce::AudioBuffer<float>&, juce::MidiBuffer&,
                                    SpectralAutomationSource&) override;
    void synchronizeAutomationFlush(SpectralAutomationSource&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 21.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    float fundamentalForDisplay() const { return engine.fundamentalForDisplay(); }
    bool copyVisualizationData(SpectralEngine::VisualizationData& destination) const
    {
        return engine.copyVisualizationData(destination);
    }
    double visualizationSampleRate() const { return engine.visualizationSampleRate(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct RawParameters
    {
        std::atomic<float>* pitchMode = nullptr;
        std::atomic<float>* monoPolyMode = nullptr;
        std::atomic<float>* polyphony = nullptr;
        std::atomic<float>* midiGate = nullptr;
        std::atomic<float>* glideMs = nullptr;
        std::atomic<float>* pitchBendRange = nullptr;
        std::atomic<float>* frequencyHz = nullptr;
        std::atomic<float>* frequencyNote = nullptr;
        std::atomic<float>* frequencyDialMode = nullptr;
        std::atomic<float>* useScale = nullptr;
        std::atomic<float>* scale = nullptr;
        std::atomic<float>* scaleRoot = nullptr;
        std::atomic<float>* transpose = nullptr;
        std::atomic<float>* transposeScaleDegrees = nullptr;
        std::atomic<float>* stretch = nullptr;
        std::atomic<float>* shift = nullptr;
        std::atomic<float>* decayMs = nullptr;
        std::atomic<float>* highFreqDamp = nullptr;
        std::atomic<float>* lowFreqDamp = nullptr;
        std::atomic<float>* modMode = nullptr;
        std::atomic<float>* modRate = nullptr;
        std::atomic<float>* pitchModAmount = nullptr;
        std::atomic<float>* harmonics = nullptr;
        std::atomic<float>* quantize = nullptr;
        std::atomic<float>* inputSendDb = nullptr;
        std::atomic<float>* unison = nullptr;
        std::atomic<float>* unisonAmount = nullptr;
        std::atomic<float>* dryWet = nullptr;
    } parameterValues;
    SpectralEngine::Settings currentSettings();
    void processAudioRange(juce::AudioBuffer<float>&, const juce::MidiBuffer&,
                           int start, int count, bool bypass,
                           juce::MidiBufferIterator* midiCursor = nullptr);
    struct AutomationCursor
    {
        int queue = 0, point = 0, count = 0, history = -1;
        int previousOffset = -1, nextOffset = 0;
        double previousValue = 0, nextValue = 0;
        bool discrete = true, hasNext = false;
    };

    std::vector<AutomationCursor> automationCursors;
    struct AutomationHistory
    {
        double normalized = 0, published = 0;
        bool valid = false;
        float integerStream = 0;
        bool integerStreamValid = false;
    };
    std::vector<AutomationHistory> automationHistory;
    struct IntegerParameterBinding
    {
        juce::RangedAudioParameter* parameter = nullptr;
        unsigned callback = 0;
    };
    std::array<IntegerParameterBinding, 3> integerParameters;
    unsigned pendingIntegerCallbacks = 0;
    void initializeIntegerStream(int index, double normalized, bool externalChange);
    void recordIntegerStream(int index, double normalized);
    std::atomic<unsigned> automationGeneration { 0 };
    unsigned audioAutomationGeneration = 0;
    void refreshAutomationGeneration();
    SpectralEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralResonatorProcessor)
};
