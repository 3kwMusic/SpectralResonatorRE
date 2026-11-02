// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#include "PluginEditor.h"

SpectralResonatorEditor::SpectralResonatorEditor(SpectralResonatorProcessor& p)
    : AudioProcessorEditor(&p), processor(p), spectrogramDisplay(p)
{
    setLookAndFeel(&modernLookAndFeel);
    addChoice(Parameters::pitchMode, Parameters::pitchModeChoices());
    addChoice(Parameters::monoPolyMode, Parameters::monoPolyChoices());
    addChoice(Parameters::polyphony, Parameters::polyphonyChoices());
    addChoice(Parameters::frequencyDialMode, Parameters::frequencyDialModeChoices());
    addChoice(Parameters::modMode, Parameters::modModeChoices());
    addChoice(Parameters::unison, Parameters::unisonChoices());
    addChoice(Parameters::scale, Parameters::scaleChoices());
    addChoice(Parameters::scaleRoot, Parameters::scaleRootChoices());
    addToggle(Parameters::midiGate, "MIDI Gate");
    addToggle(Parameters::useScale, "Use Scale");
    addToggle(Parameters::quantize, "Quantize");
    polyphonyLabel.setText("Polyphony", juce::dontSendNotification);
    polyphonyLabel.setColour(juce::Label::textColourId, SpectralPalette::mutedText);
    polyphonyLabel.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(polyphonyLabel);
    configureKnob(glideSlider, Parameters::glideMs, "Glide", glideLabel);
    configureKnob(pitchBendSlider, Parameters::pitchBendRange, "PB Range", pitchBendLabel);
    configureKnob(frequencySlider, Parameters::frequencyHz, "Freq", frequencyLabel);
    configureKnob(frequencyNoteSlider, Parameters::frequencyNote, "Freq", frequencyNoteLabel);
    configureKnob(transposeSlider, Parameters::transpose, "Transp.", transposeLabel);
    configureKnob(transposeScaleSlider, Parameters::transposeScaleDegrees, "SD Shift", transposeScaleLabel);
    configureHorizontalSlider(stretchSlider, Parameters::stretch, "Stretch", stretchLabel);
    configureHorizontalSlider(shiftSlider, Parameters::shift, "Shift", shiftLabel);
    configureKnob(decaySlider, Parameters::decayMs, "Decay", decayLabel);
    configureKnob(highDampSlider, Parameters::highFreqDamp, "HF Damp", highDampLabel);
    configureKnob(lowDampSlider, Parameters::lowFreqDamp, "LF Damp", lowDampLabel);
    configureKnob(modRateSlider, Parameters::modRate, "Mod Rate", modRateLabel);
    configureKnob(pitchModSlider, Parameters::pitchModAmount, "Pch. Mod", pitchModLabel);
    configureHorizontalSlider(harmonicsSlider, Parameters::harmonics, "Harmonics", harmonicsLabel);
    configureKnob(inputSendSlider, Parameters::inputSendDb, "Input Send", inputSendLabel);
    configureKnob(unisonAmountSlider, Parameters::unisonAmount, "Uni. Amt", unisonAmountLabel);
    configureKnob(dryWetSlider, Parameters::dryWet, "Dry / Wet", dryWetLabel);
    for (auto* tab : {&spectrogramTab, &spectrumTab})
    {
        tab->setClickingTogglesState(true);
        tab->setRadioGroupId(9701);
        tab->setColour(juce::TextButton::buttonColourId, SpectralPalette::background);
        tab->setColour(juce::TextButton::buttonOnColourId, SpectralPalette::border);
        tab->setColour(juce::TextButton::textColourOnId, SpectralPalette::text);
        tab->setColour(juce::TextButton::textColourOffId, SpectralPalette::mutedText);
        tab->onClick = [this]
        {
            spectrogramDisplay.setVisible(spectrogramTab.getToggleState());
            repaint(partialDisplayBounds);
        };
        addAndMakeVisible(*tab);
    }
    spectrogramTab.setToggleState(true, juce::dontSendNotification);
    spectrogramTab.setTooltip("Frequency across, time upwards. Dry: yellow; wet: blue.");
    spectrumTab.setTooltip("Input FFT spectrum and resonant partial positions");
    addAndMakeVisible(spectrogramDisplay);
    setSize(880, 500);
    startTimerHz(15);
    updateControlStates();
}

SpectralResonatorEditor::~SpectralResonatorEditor() { setLookAndFeel(nullptr); }

void SpectralResonatorEditor::configureKnob(juce::Slider& slider, const juce::String& id,
                                            const juce::String& name, juce::Label& label)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setComponentID(id);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 88, 18);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.22f,
                               juce::MathConstants<float>::pi * 2.78f, true);
    slider.setColour(juce::Slider::rotarySliderFillColourId, SpectralPalette::accent);
    slider.setColour(juce::Slider::thumbColourId, SpectralPalette::text);
    addAndMakeVisible(slider);
    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, SpectralPalette::mutedText);
    label.setFont(juce::FontOptions(11.5f));
    addAndMakeVisible(label);
    sliderAttachments.push_back(std::make_unique<SliderAttachment>(processor.apvts, id, slider));
}

void SpectralResonatorEditor::configureHorizontalSlider(juce::Slider& slider, const juce::String& id,
                                                        const juce::String& name, juce::Label& label)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setComponentID(id);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
    slider.setColour(juce::Slider::trackColourId, SpectralPalette::accent);
    slider.setColour(juce::Slider::thumbColourId, SpectralPalette::text);
    addAndMakeVisible(slider);
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, SpectralPalette::mutedText);
    label.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(label);
    sliderAttachments.push_back(std::make_unique<SliderAttachment>(processor.apvts, id, slider));
}

juce::ComboBox& SpectralResonatorEditor::addChoice(const juce::String& id, const juce::StringArray& items)
{
    juce::ComboBox* box = nullptr;
    if (id == Parameters::pitchMode) box = &pitchModeBox;
    else if (id == Parameters::monoPolyMode) box = &monoPolyBox;
    else if (id == Parameters::polyphony) box = &polyphonyBox;
    else if (id == Parameters::frequencyDialMode) box = &frequencyDialModeBox;
    else if (id == Parameters::modMode) box = &modModeBox;
    else if (id == Parameters::unison) box = &unisonBox;
    else if (id == Parameters::scale) box = &scaleBox;
    else box = &scaleRootBox;
    box->addItemList(items, 1);
    addAndMakeVisible(*box);
    comboAttachments.push_back(std::make_unique<ComboBoxAttachment>(processor.apvts, id, *box));
    return *box;
}

juce::ToggleButton& SpectralResonatorEditor::addToggle(const juce::String& id, const juce::String& name)
{
    juce::ToggleButton* button = nullptr;
    if (id == Parameters::midiGate) button = &midiGateButton;
    else if (id == Parameters::useScale) button = &useScaleButton;
    else button = &quantizeButton;
    button->setButtonText(name);
    button->setColour(juce::ToggleButton::textColourId, SpectralPalette::text);
    addAndMakeVisible(*button);
    buttonAttachments.push_back(std::make_unique<ButtonAttachment>(processor.apvts, id, *button));
    return *button;
}

void SpectralResonatorEditor::timerCallback()
{
    SpectralEngine::VisualizationData snapshot;
    if (processor.copyVisualizationData(snapshot))
        visualizationData = snapshot;
    updateControlStates();
    repaint();
}

void SpectralResonatorEditor::updateControlStates()
{
    auto raw = [this](const juce::String& id) { return processor.apvts.getRawParameterValue(id)->load(); };
    const auto midi = static_cast<int>(raw(Parameters::pitchMode)) == static_cast<int>(Parameters::PitchMode::midi);
    const auto poly = static_cast<int>(raw(Parameters::monoPolyMode)) == static_cast<int>(Parameters::MonoPolyMode::poly);
    const auto scale = raw(Parameters::useScale) >= 0.5f;
    const auto noteDial = static_cast<int>(raw(Parameters::frequencyDialMode)) == 1;
    const auto modulated = static_cast<int>(raw(Parameters::modMode)) != static_cast<int>(Parameters::ModMode::none);
    useScaleButton.setEnabled(true);
    useScaleButton.setTooltip("Quantize to the selected manual scale and root");
    scaleBox.setVisible(scale);
    scaleRootBox.setVisible(scale);
    for (auto* component : { static_cast<juce::Component*>(&glideSlider), static_cast<juce::Component*>(&glideLabel),
                             static_cast<juce::Component*>(&pitchBendSlider), static_cast<juce::Component*>(&pitchBendLabel) })
        component->setVisible(!scale);
    monoPolyBox.setEnabled(midi);
    polyphonyBox.setEnabled(midi && poly);
    midiGateButton.setEnabled(midi && !poly);
    glideSlider.setEnabled(midi && !poly);
    pitchBendSlider.setEnabled(midi);
    const auto showFrequency = !midi && !scale;
    // Keep the active component visible across timer ticks. Hiding a Slider
    // closes its text editor even if it is shown again in the same callback.
    frequencySlider.setVisible(showFrequency && !noteDial);
    frequencyLabel.setVisible(showFrequency && !noteDial);
    frequencyNoteSlider.setVisible(showFrequency && noteDial);
    frequencyNoteLabel.setVisible(showFrequency && noteDial);
    transposeSlider.setVisible(midi && !scale);
    transposeLabel.setVisible(midi && !scale);
    transposeScaleSlider.setVisible(scale);
    transposeScaleLabel.setVisible(scale);
    frequencyDialModeBox.setVisible(showFrequency);
    modRateSlider.setEnabled(modulated);
    pitchModSlider.setEnabled(modulated);
}

float SpectralResonatorEditor::displayedFundamental() const
{
    auto raw = [this](const juce::String& id) { return processor.apvts.getRawParameterValue(id)->load(); };
    auto note = raw(Parameters::frequencyNote);
    auto fundamental = 0.0f;
    if (static_cast<int>(raw(Parameters::pitchMode)) == static_cast<int>(Parameters::PitchMode::midi))
        fundamental = processor.fundamentalForDisplay();
    else if (static_cast<int>(raw(Parameters::frequencyDialMode)) == 0 && raw(Parameters::useScale) < 0.5f)
        fundamental = raw(Parameters::frequencyHz);
    else
    {
        if (raw(Parameters::useScale) >= 0.5f)
            note = Parameters::mapNoteToScale(static_cast<int>(raw(Parameters::scale)),
                static_cast<int>(raw(Parameters::scaleRoot)), note,
                static_cast<int>(raw(Parameters::transposeScaleDegrees)));
        fundamental = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    }
    return fundamental * std::pow(2.0f, std::round(raw(Parameters::shift)) / 12.0f);
}

void SpectralResonatorEditor::drawPartialDisplay(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(SpectralPalette::background.darker(0.35f));
    g.fillRoundedRectangle(bounds.toFloat(), 7.0f);
    auto frequencyX = [bounds](float frequency)
    {
        const auto low = std::log10(20.0f);
        const auto high = std::log10(20000.0f);
        const auto proportion = (std::log10(juce::jlimit(20.0f, 20000.0f, frequency)) - low) / (high - low);
        return bounds.getX() + proportion * static_cast<float>(bounds.getWidth());
    };
    g.setFont(juce::FontOptions(10.0f));
    for (const auto frequency : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                  2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const auto x = frequencyX(frequency);
        g.setColour(SpectralPalette::border.withAlpha(0.55f));
        g.drawLine(x, static_cast<float>(bounds.getY()), x, static_cast<float>(bounds.getBottom()), 0.7f);
        g.setColour(SpectralPalette::mutedText);
        const auto text = frequency >= 1000.0f
            ? juce::String(frequency / 1000.0f, frequency >= 10000.0f ? 0 : 1) + "k"
            : juce::String(static_cast<int>(frequency));
        g.drawText(text, static_cast<int>(x) - 18, bounds.getBottom() - 18, 36, 14, juce::Justification::centred);
    }
    for (auto row = 1; row < 4; ++row)
    {
        const auto y = bounds.getY() + row * bounds.getHeight() / 4;
        g.setColour(SpectralPalette::border.withAlpha(0.35f));
        g.drawLine(static_cast<float>(bounds.getX()), static_cast<float>(y),
                   static_cast<float>(bounds.getRight()), static_cast<float>(y), 0.7f);
    }
    const auto sampleRate = processor.visualizationSampleRate();
    for (int channel = 0; channel < 2; ++channel)
    {
        juce::Path spectrum;
        auto started = false;
        for (int bin = 1; bin < SpectralEngine::visualizationBinCount - 1; ++bin)
        {
            const auto frequency = static_cast<float>(bin * sampleRate / 1024.0);
            if (frequency < 20.0f || frequency > 20000.0f) continue;
            const auto magnitude = visualizationData[channel * SpectralEngine::visualizationBinCount + bin];
            const auto level = juce::jlimit(0.0f, 1.0f,
                (20.0f * std::log10(juce::jmax(1.0e-6f, magnitude)) + 80.0f) / 80.0f);
            const auto x = frequencyX(frequency);
            const auto y = static_cast<float>(bounds.getBottom() - 22)
                - level * static_cast<float>(bounds.getHeight() - 52);
            if (!started) { spectrum.startNewSubPath(x, y); started = true; }
            else spectrum.lineTo(x, y);
        }
        g.setColour(channel == 0 ? SpectralPalette::accent.withAlpha(0.75f)
                                 : SpectralPalette::partialAccent.withAlpha(0.55f));
        g.strokePath(spectrum, juce::PathStrokeType(1.0f));
    }
    const auto fundamental = displayedFundamental();
    const auto stretch = processor.apvts.getRawParameterValue(Parameters::stretch)->load();
    const auto count = juce::roundToInt(processor.apvts.getRawParameterValue(Parameters::harmonics)->load());
    const auto stride = juce::jmax(1, (count + 63) / 64);
    for (auto harmonic = 1; harmonic <= count; harmonic += stride)
    {
        const auto ratio = 1.0f + (1.0f + stretch * 0.01f) * (harmonic - 1);
        const auto frequency = fundamental * ratio;
        if (frequency < 20.0f || frequency > 20000.0f) continue;
        const auto x = frequencyX(frequency);
        const auto level = 1.0f / (1.0f + 0.065f * static_cast<float>(harmonic));
        const auto height = level * static_cast<float>(bounds.getHeight() - 52);
        g.setColour(SpectralPalette::partialAccent.withAlpha(0.28f + 0.62f * level));
        g.drawLine(x, static_cast<float>(bounds.getBottom() - 22) - height,
                   x, static_cast<float>(bounds.getBottom() - 22), harmonic == 1 ? 2.0f : 1.0f);
    }
    g.setColour(SpectralPalette::mutedText);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("INPUT FFT + RESONANT PARTIALS", bounds.getX() + 12, bounds.getY() + 8, 220, 16, juce::Justification::left);
    g.drawText(juce::String(fundamental, 2) + " Hz  /  " + juce::String(count) + " harmonics",
               bounds.getRight() - 218, bounds.getY() + 8, 206, 16, juce::Justification::right);
}

void SpectralResonatorEditor::paint(juce::Graphics& g)
{
    g.fillAll(SpectralPalette::background);
    g.setColour(SpectralPalette::text);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("SPECTRAL RESONATOR", 18, 9, 240, 28, juce::Justification::left);
    g.setColour(SpectralPalette::partialAccent);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("RE", 207, 14, 28, 18, juce::Justification::centred);
    g.setColour(SpectralPalette::mutedText);
    g.setFont(juce::FontOptions(9.0f));
    g.drawText("TUNED RESONANCES AND HARMONIC MODULATION", 460, 14, 406, 18, juce::Justification::right);
    for (const auto& area : { juce::Rectangle<int>(12, 48, 252, 440),
                              juce::Rectangle<int>(274, 48, 398, 440),
                              juce::Rectangle<int>(682, 48, 186, 440) })
    {
        g.setColour(SpectralPalette::panel);
        g.fillRoundedRectangle(area.toFloat(), 8.0f);
        g.setColour(SpectralPalette::border.withAlpha(0.7f));
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 8.0f, 1.0f);
    }
    g.setColour(SpectralPalette::mutedText);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("PITCH AND DECAY", 24, 54, 228, 14, juce::Justification::left);
    g.drawText("PARTIALS AND MODULATION", 286, 54, 374, 14, juce::Justification::left);
    g.drawText("OUTPUT", 694, 54, 162, 14, juce::Justification::left);
    g.drawText("UNISON", 694, 180, 162, 14, juce::Justification::left);
    if (!spectrogramTab.getToggleState()) drawPartialDisplay(g, partialDisplayBounds.withTrimmedTop(28));
}

void SpectralResonatorEditor::resized()
{
    pitchModeBox.setBounds(24, 76, 228, 24);
    monoPolyBox.setBounds(24, 116, 108, 22);
    polyphonyLabel.setBounds(144, 102, 108, 12);
    polyphonyBox.setBounds(144, 116, 108, 22);
    midiGateButton.setBounds(22, 142, 112, 20);
    useScaleButton.setBounds(142, 142, 112, 20);
    auto placeKnob = [] (juce::Slider& slider, juce::Label& label, int x, int y, int w = 108, int h = 64)
    {
        label.setBounds(x, y, w, 14);
        slider.setBounds(x + 4, y + 16, w - 8, h);
    };
    placeKnob(glideSlider, glideLabel, 24, 166);
    placeKnob(pitchBendSlider, pitchBendLabel, 144, 166);
    scaleRootBox.setBounds(24, 184, 64, 24);
    scaleBox.setBounds(96, 184, 156, 24);
    frequencyDialModeBox.setBounds(24, 250, 108, 22);
    placeKnob(frequencySlider, frequencyLabel, 24, 278);
    placeKnob(frequencyNoteSlider, frequencyNoteLabel, 24, 278);
    placeKnob(transposeSlider, transposeLabel, 24, 278);
    placeKnob(transposeScaleSlider, transposeScaleLabel, 24, 278);
    placeKnob(decaySlider, decayLabel, 144, 278);
    placeKnob(highDampSlider, highDampLabel, 24, 362);
    placeKnob(lowDampSlider, lowDampLabel, 144, 362);
    stretchLabel.setBounds(24, 448, 54, 18);
    stretchSlider.setBounds(78, 446, 174, 22);
    shiftLabel.setBounds(24, 470, 54, 18);
    shiftSlider.setBounds(78, 468, 174, 20);
    partialDisplayBounds = { 286, 76, 374, 238 };
    spectrogramTab.setBounds(286, 76, 126, 22);
    spectrumTab.setBounds(416, 76, 164, 22);
    spectrogramDisplay.setBounds(partialDisplayBounds.withTrimmedTop(28));
    modModeBox.setBounds(286, 322, 374, 24);
    placeKnob(modRateSlider, modRateLabel, 286, 354, 180);
    placeKnob(pitchModSlider, pitchModLabel, 480, 354, 180);
    harmonicsLabel.setBounds(286, 448, 70, 18);
    harmonicsSlider.setBounds(356, 446, 208, 22);
    quantizeButton.setBounds(570, 446, 90, 22);
    placeKnob(inputSendSlider, inputSendLabel, 694, 76, 162, 70);
    unisonBox.setBounds(694, 198, 162, 24);
    placeKnob(unisonAmountSlider, unisonAmountLabel, 694, 238, 162, 70);
    placeKnob(dryWetSlider, dryWetLabel, 694, 350, 162, 96);
}
