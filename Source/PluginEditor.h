// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "PluginProcessor.h"
#include "ModernLookAndFeel.h"
#include "SpectrogramDisplay.h"

class SpectralResonatorEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit SpectralResonatorEditor(SpectralResonatorProcessor& processor);
    ~SpectralResonatorEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void configureKnob(juce::Slider&, const juce::String&, const juce::String&, juce::Label&);
    void configureHorizontalSlider(juce::Slider&, const juce::String&, const juce::String&, juce::Label&);
    juce::ComboBox& addChoice(const juce::String& id, const juce::StringArray& items);
    juce::ToggleButton& addToggle(const juce::String& id, const juce::String& name);
    void timerCallback() override;
    void updateControlStates();
    void drawPartialDisplay(juce::Graphics& g, juce::Rectangle<int> bounds);
    float displayedFundamental() const;

    SpectralResonatorProcessor& processor;
    ModernLookAndFeel modernLookAndFeel;
    SpectrogramDisplay spectrogramDisplay;
    juce::TextButton spectrogramTab {"Spectrogram"}, spectrumTab {"Spectrum / Partials"};
    juce::ComboBox pitchModeBox, monoPolyBox, polyphonyBox, frequencyDialModeBox, modModeBox, unisonBox, scaleBox, scaleRootBox;
    juce::ToggleButton midiGateButton, useScaleButton, quantizeButton;
    juce::Slider glideSlider, pitchBendSlider, frequencySlider, frequencyNoteSlider;
    juce::Slider transposeSlider, transposeScaleSlider, stretchSlider, shiftSlider, decaySlider;
    juce::Slider highDampSlider, lowDampSlider, modRateSlider, pitchModSlider, harmonicsSlider;
    juce::Slider inputSendSlider, unisonAmountSlider, dryWetSlider;
    juce::Label polyphonyLabel, glideLabel, pitchBendLabel, frequencyLabel, frequencyNoteLabel;
    juce::Label transposeLabel, transposeScaleLabel, stretchLabel, shiftLabel, decayLabel;
    juce::Label highDampLabel, lowDampLabel, modRateLabel, pitchModLabel, harmonicsLabel;
    juce::Label inputSendLabel, unisonAmountLabel, dryWetLabel;
    juce::Rectangle<int> partialDisplayBounds;
    SpectralEngine::VisualizationData visualizationData {};
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralResonatorEditor)
};
