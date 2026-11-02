// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#include "ModernLookAndFeel.h"

ModernLookAndFeel::ModernLookAndFeel()
{
    setColour(juce::ComboBox::backgroundColourId, SpectralPalette::background);
    setColour(juce::ComboBox::textColourId, SpectralPalette::text);
    setColour(juce::ComboBox::outlineColourId, SpectralPalette::border);
    setColour(juce::PopupMenu::backgroundColourId, SpectralPalette::panel);
    setColour(juce::PopupMenu::textColourId, SpectralPalette::text);
    setColour(juce::Slider::textBoxTextColourId, SpectralPalette::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void ModernLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider& slider)
{
    // JUCE already removes the text-box area before calling this method.
    // Reserving it again made the shorter HF/LF Damp dials almost disappear.
    const auto diameter = juce::jmax(8.0f, juce::jmin(static_cast<float>(width), static_cast<float>(height)) - 4.0f);
    const auto radius = diameter * 0.5f;
    const auto centre = juce::Point<float>(x + width * 0.5f, y + height * 0.5f);
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    g.setColour(SpectralPalette::background);
    g.fillEllipse(centre.x - radius, centre.y - radius, diameter, diameter);
    g.setColour(SpectralPalette::border);
    g.drawEllipse(centre.x - radius, centre.y - radius, diameter, diameter, 2.0f);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(SpectralPalette::border);
    g.strokePath(track, juce::PathStrokeType(3.0f));

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f,
                        rotaryStartAngle, angle, true);
    g.setColour(slider.isEnabled() ? SpectralPalette::accent : SpectralPalette::mutedText);
    g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addRoundedRectangle(-1.5f, -radius + 8.0f, 3.0f, radius * 0.58f, 1.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    g.setColour(SpectralPalette::text);
    g.fillPath(pointer);
}
