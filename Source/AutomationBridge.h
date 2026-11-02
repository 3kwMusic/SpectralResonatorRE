// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Borrowed, read-only VST3 queue view. Its lifetime is exactly one host process()
// call. No SDK type leaks into the DSP or processor, and no event buffer is copied.
// MIDI-controller parameter queues remain on JUCE's existing MIDI path.
class SpectralAutomationSource
{
public:
    virtual ~SpectralAutomationSource() = default;
    virtual int queueCount() const noexcept = 0;
    virtual int pointCount(int queue) const noexcept = 0;
    virtual bool getPoint(int queue, int point, int& offset, double& normalized) const noexcept = 0;
    virtual double currentNormalized(int queue) const noexcept = 0;
    virtual bool isDiscrete(int queue) const noexcept = 0;
    virtual int parameterIndex(int queue) const noexcept = 0;
    virtual void applyPoint(int queue, double normalized) noexcept = 0;
    virtual bool isBypassed() const noexcept = 0;
};

class SpectralAutomationConsumer
{
public:
    virtual ~SpectralAutomationConsumer() = default;
    virtual void processBlockWithAutomation(juce::AudioBuffer<float>&,
                                            juce::MidiBuffer&,
                                            SpectralAutomationSource&) = 0;
    virtual void synchronizeAutomationFlush(SpectralAutomationSource&) = 0;
};
