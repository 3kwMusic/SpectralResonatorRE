// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace SpectralPalette
{
const juce::Colour background { 0xff15191f };
const juce::Colour panel { 0xff20262e };
const juce::Colour border { 0xff3b4652 };
const juce::Colour text { 0xffe5edf3 };
const juce::Colour mutedText { 0xff91a0ad };
const juce::Colour accent { 0xff5ba8d8 };
const juce::Colour partialAccent { 0xff8bd4b0 };
}

class ModernLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;
};
