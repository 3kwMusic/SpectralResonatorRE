// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#include "Parameters.h"
#include <array>
#include <cmath>

namespace Parameters
{
struct ScaleDefinition
{
    const char* name;
    std::uint8_t size;
    std::array<std::uint8_t, 12> notes;
};

static constexpr std::array<ScaleDefinition, 36> scaleDefinitions {{
    { "Chromatic", 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    { "Dorian", 7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "Mixolydian", 7, { 0, 2, 4, 5, 7, 9, 10 } },
    { "Major", 7, { 0, 2, 4, 5, 7, 9, 11 } },
    { "Minor", 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { "Locrian", 7, { 0, 1, 3, 5, 6, 8, 10 } },
    { "Whole Tone", 6, { 0, 2, 4, 6, 8, 10 } },
    { "Lydian", 7, { 0, 2, 4, 6, 7, 9, 11 } },
    { "Phrygian", 7, { 0, 1, 3, 5, 7, 8, 10 } },
    { "Iwato", 5, { 0, 1, 5, 6, 10 } },
    { "Kumoi", 5, { 0, 2, 3, 7, 9 } },
    { "Hirajoshi", 5, { 0, 2, 3, 7, 8 } },
    { "In-Sen", 5, { 0, 1, 5, 7, 10 } },
    { "Messiaen 3", 9, { 0, 2, 3, 4, 6, 7, 8, 10, 11 } },
    { "Messiaen 4", 8, { 0, 1, 2, 5, 6, 7, 8, 11 } },
    { "Pelog Selisir", 5, { 0, 1, 3, 7, 8 } },
    { "Pelog Tembung", 5, { 0, 2, 5, 7, 8 } },
    { "Messiaen 7", 10, { 0, 1, 2, 3, 5, 6, 7, 8, 9, 11 } },
    { "Messiaen 5", 6, { 0, 1, 5, 6, 7, 11 } },
    { "Messiaen 6", 8, { 0, 2, 4, 5, 6, 8, 10, 11 } },
    { "Minor Blues", 6, { 0, 3, 5, 6, 7, 10 } },
    { "Minor Pentatonic", 5, { 0, 3, 5, 7, 10 } },
    { "Half-whole Dim.", 8, { 0, 1, 3, 4, 6, 7, 9, 10 } },
    { "Whole-half Dim.", 8, { 0, 2, 3, 5, 6, 8, 9, 11 } },
    { "Harmonic Major", 7, { 0, 2, 4, 5, 7, 8, 11 } },
    { "Dorian #4", 7, { 0, 2, 3, 6, 7, 9, 10 } },
    { "Major Pentatonic", 5, { 0, 2, 4, 7, 9 } },
    { "Harmonic Minor", 7, { 0, 2, 3, 5, 7, 8, 11 } },
    { "Lydian Augmented", 7, { 0, 2, 4, 6, 8, 9, 11 } },
    { "Lydian Dominant", 7, { 0, 2, 4, 6, 7, 9, 10 } },
    { "Phrygian Dominant", 7, { 0, 1, 4, 5, 7, 8, 10 } },
    { "Melodic Minor", 7, { 0, 2, 3, 5, 7, 9, 11 } },
    { "Bhairav", 7, { 0, 1, 4, 5, 7, 8, 11 } },
    { "Hungarian Minor", 7, { 0, 2, 3, 6, 7, 8, 11 } },
    { "Super Locrian", 7, { 0, 1, 3, 4, 6, 8, 10 } },
    { "8-Tone Spanish", 8, { 0, 1, 3, 4, 5, 6, 8, 10 } }
}};

juce::StringArray scaleChoices()
{
    juce::StringArray choices;
    for (const auto& definition : scaleDefinitions) choices.add(definition.name);
    return choices;
}

juce::StringArray scaleRootChoices()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

int scaleNoteCount(int choiceIndex)
{
    return scaleDefinitions[static_cast<size_t>(juce::jlimit(0,
        static_cast<int>(scaleDefinitions.size()) - 1, choiceIndex))].size;
}

float mapNoteToScale(int choiceIndex, int root, float note, int degreeShift)
{
    const auto& definition = scaleDefinitions[static_cast<size_t>(juce::jlimit(0,
        static_cast<int>(scaleDefinitions.size()) - 1, choiceIndex))];
    root = juce::jlimit(0, 11, root);
    const auto relative = note - static_cast<float>(root);
    const auto octave = static_cast<int>(std::floor(relative / 12.0f));
    const auto pitch = relative - static_cast<float>(octave * 12);
    auto bestDistance = 1000.0f;
    auto bestDegree = 0;
    auto bestOctave = octave;
    for (int degree = 0; degree < definition.size; ++degree)
    {
        for (const auto offset : { -12, 0, 12 })
        {
            const auto candidate = static_cast<float>(definition.notes[degree] + offset);
            const auto distance = std::abs(pitch - candidate);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestDegree = degree;
                bestOctave = octave + offset / 12;
            }
        }
    }
    const auto shifted = bestDegree + degreeShift;
    const auto degreeOctave = static_cast<int>(std::floor(static_cast<float>(shifted) / definition.size));
    const auto degree = shifted - degreeOctave * definition.size;
    return static_cast<float>(root + (bestOctave + degreeOctave) * 12 + definition.notes[degree]);
}

juce::StringArray pitchModeChoices() { return { "Internal", "MIDI" }; }
juce::StringArray monoPolyChoices() { return { "Mono", "Poly" }; }
juce::StringArray polyphonyChoices() { return { "2", "4", "8", "16" }; }
juce::StringArray modModeChoices() { return { "None", "Chorus", "Wander", "Granular" }; }
juce::StringArray unisonChoices() { return { "1", "2", "4", "8" }; }
juce::StringArray frequencyDialModeChoices() { return { "Hz", "Note" }; }

class PolyphonyParameter final : public juce::AudioParameterFloat
{
public:
    PolyphonyParameter()
        : AudioParameterFloat(polyphony, "Polyphony",
            juce::NormalisableRange<float>(2.0f, 16.0f,
                [] (float, float, float n)
                { return static_cast<float>(1 << (1 + juce::jlimit(0, 3, juce::roundToInt(n * 3)))); },
                [] (float, float, float count)
                { return juce::jlimit(0.0f, 1.0f, (std::log2(juce::jmax(2.0f, count)) - 1) / 3); },
                [] (float, float, float count)
                { return std::exp2(static_cast<float>(juce::jlimit(1, 4,
                    juce::roundToInt(std::log2(juce::jmax(2.0f, count)))))); }),
            8.0f, juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction([] (float count, int) { return juce::String(juce::roundToInt(count)); })
                .withValueFromStringFunction([] (const juce::String& text) { return text.getFloatValue(); }))
    {}

    int getNumSteps() const override { return 4; }
    bool isDiscrete() const override { return true; }
};

class SteppedIntegerParameter final : public juce::AudioParameterInt
{
public:
    using AudioParameterInt::AudioParameterInt;
    bool isDiscrete() const override { return true; }
};

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto logarithmicRange = [] (float minimum, float maximum)
    {
        return juce::NormalisableRange<float>(minimum, maximum,
            [] (float lo, float hi, float n)
            {
                if (n <= 0.0f) return lo;
                if (n >= 1.0f) return hi;
                return std::exp2(std::fma(n, std::log2(hi) - std::log2(lo), std::log2(lo)));
            },
            [] (float lo, float hi, float value)
            {
                return juce::jlimit(0.0f, 1.0f,
                    (std::log2(juce::jmax(lo, value)) - std::log2(lo))
                        / (std::log2(hi) - std::log2(lo)));
            });
    };
    const auto percent = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
        [] (float value, int) { return juce::String(value, 1) + "%"; });
    const auto semitones = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
        [] (float value, int) { return juce::String(value, 2) + " st"; });

    const auto integerCurveRange = [] (float minimum, float maximum)
    {
        auto range = juce::NormalisableRange<float>(minimum, maximum,
            [] (float lo, float hi, float n) { return std::fma(n, hi - lo, lo); },
            [] (float lo, float hi, float value) { return (value - lo) / (hi - lo); },
            [] (float lo, float hi, float value) { return juce::jlimit(lo, hi, std::round(value)); });
        range.interval = 1.0f;
        return range;
    };
    const auto milliseconds = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
        [] (float value, int) { return juce::String(value, 2) + " ms"; });

    layout.add(std::make_unique<juce::AudioParameterChoice>(pitchMode, "Pitch Mode", pitchModeChoices(), 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(monoPolyMode, "Mono/Poly", monoPolyChoices(), 0));

    layout.add(std::make_unique<PolyphonyParameter>());
    layout.add(std::make_unique<juce::AudioParameterBool>(midiGate, "MIDI Gate", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(glideMs, "Glide",
        juce::NormalisableRange<float>(0.0f, 20000.0f, 0.0f, 0.25f), 0.0f, milliseconds));
    layout.add(std::make_unique<SteppedIntegerParameter>(pitchBendRange, "Pitch Bend Range", 0, 24, 2,
        juce::AudioParameterIntAttributes().withLabel("st")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(frequencyHz, "Frequency",
        logarithmicRange(8.2f, 2000.0f), 110.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(frequencyNote, "Frequency Note",
        integerCurveRange(0.0f, 96.0f), 45.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(frequencyDialMode, "Frequency Dial Mode", frequencyDialModeChoices(), 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(useScale, "Use Scale", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(scale, "Scale", scaleChoices(), 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(scaleRoot, "Scale Root", scaleRootChoices(), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(transpose, "Transpose",
        integerCurveRange(-48.0f, 48.0f), 0.0f, semitones));
    layout.add(std::make_unique<juce::AudioParameterFloat>(transposeScaleDegrees, "Scale Degree Shift",
        integerCurveRange(-28.0f, 28.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int) { return juce::String(static_cast<int>(value)); })));
    layout.add(std::make_unique<juce::AudioParameterFloat>(stretch, "Stretch",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.0f), 0.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(shift, "Shift",
        juce::NormalisableRange<float>(-48.0f, 48.0f, 0.0f), 0.0f, semitones));
    layout.add(std::make_unique<juce::AudioParameterFloat>(decayMs, "Decay",
        juce::NormalisableRange<float>(1.0f, 20000.0f, 0.0f, 1.0f / 3.0f), 222.0f, milliseconds));
    layout.add(std::make_unique<juce::AudioParameterFloat>(highFreqDamp, "HF Damp",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.0f), 0.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(lowFreqDamp, "LF Damp",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.0f), 0.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterChoice>(modMode, "Modulation Mode", modModeChoices(), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(modRate, "Mod Rate",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.0f, 1.0f / 3.33f), 10.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(pitchModAmount, "Pitch Mod",
        juce::NormalisableRange<float>(0.0f, 4.0f, 0.0f, 1.0f / 3.33f), 0.0f, semitones));
    layout.add(std::make_unique<juce::AudioParameterFloat>(harmonics, "Harmonics",
        logarithmicRange(1.0f, 256.0f), 256.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int) { return juce::String(juce::roundToInt(value)); })));
    layout.add(std::make_unique<juce::AudioParameterBool>(quantize, "Quantize", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(inputSendDb, "Input Send",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(unison, "Unison", unisonChoices(), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(unisonAmount, "Unison Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.0f, 0.25f), 10.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(dryWet, "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })
            .withValueFromStringFunction([] (const juce::String& text)
                { return juce::jlimit(0.0f, 1.0f, text.getFloatValue() * 0.01f); })));
    return layout;
}

int unisonVoices(int choiceIndex)
{
    static constexpr int voices[] { 1, 2, 4, 8 };
    return voices[juce::jlimit(0, 3, choiceIndex)];
}
}
