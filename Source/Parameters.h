// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Parameters
{
inline const juce::String pitchMode { "pitchMode" };
inline const juce::String monoPolyMode { "monoPolyMode" };
inline const juce::String polyphony { "polyphony" };
inline const juce::String midiGate { "midiGate" };
inline const juce::String glideMs { "glideMs" };
inline const juce::String pitchBendRange { "pitchBendRange" };
inline const juce::String frequencyHz { "frequencyHz" };
inline const juce::String frequencyNote { "frequencyNote" };
inline const juce::String frequencyDialMode { "frequencyDialMode" };
inline const juce::String useScale { "useScale" };
inline const juce::String scale { "scale" };
inline const juce::String scaleRoot { "scaleRoot" };
inline const juce::String transpose { "transpose" };
inline const juce::String transposeScaleDegrees { "transposeScaleDegrees" };
inline const juce::String stretch { "stretch" };
inline const juce::String shift { "shift" };
inline const juce::String decayMs { "decayMs" };
inline const juce::String highFreqDamp { "highFreqDamp" };
inline const juce::String lowFreqDamp { "lowFreqDamp" };
inline const juce::String modMode { "modMode" };
inline const juce::String modRate { "modRate" };
inline const juce::String pitchModAmount { "pitchModAmount" };
inline const juce::String harmonics { "harmonics" };
inline const juce::String quantize { "quantize" };
inline const juce::String inputSendDb { "inputSendDb" };
inline const juce::String unison { "unison" };
inline const juce::String unisonAmount { "unisonAmount" };
inline const juce::String dryWet { "dryWet" };

enum class PitchMode { internal = 0, midi };
enum class MonoPolyMode { mono = 0, poly };
enum class ModMode { none = 0, chorus, wander, granular };

juce::StringArray pitchModeChoices();
juce::StringArray monoPolyChoices();
juce::StringArray polyphonyChoices();
juce::StringArray modModeChoices();
juce::StringArray unisonChoices();
juce::StringArray frequencyDialModeChoices();
juce::StringArray scaleChoices();
juce::StringArray scaleRootChoices();
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
int unisonVoices(int choiceIndex);
int scaleNoteCount(int choiceIndex);
float mapNoteToScale(int choiceIndex, int root, float note, int degreeShift = 0);
}
