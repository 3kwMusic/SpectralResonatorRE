// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "PluginProcessor.h"

// All FFT work, colour mapping, and image scrolling run on the message thread.
class SpectrogramDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit SpectrogramDisplay(SpectralResonatorProcessor& owner) : processor(owner)
    {
        setInterceptsMouseClicks(false, false);
        for (int n = 0; n < fftSize; ++n)
        {
            window[n] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * n / fftSize);
            windowSum += window[n];
        }
        clearHistory();
        processor.setSpectrogramActive(true);
        startTimerHz(30);
    }
    ~SpectrogramDisplay() override { processor.setSpectrogramActive(false); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(background);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImage(history, getLocalBounds().toFloat());
        // Older time slices become less prominent, as in the supplied reference.
        g.setGradientFill(juce::ColourGradient(background.withAlpha(0.55f), 0, 0,
            background.withAlpha(0.0f), 0, static_cast<float>(getHeight()), false));
        g.fillAll();
        g.setColour(juce::Colour(0xff91a0ad));
        g.drawRect(getLocalBounds(), 1);
    }

private:
    static constexpr int fftSize = 2048, bins = fftSize / 2 + 1;
    static constexpr int imageWidth = 508, imageHeight = 226;
    const juce::Colour background {0xff0c1217};
    SpectralResonatorProcessor& processor;
    juce::dsp::FFT fft {11};
    juce::Image history {juce::Image::RGB, imageWidth, imageHeight, true};
    std::array<std::array<float, fftSize>, 4> ring {};
    std::array<std::array<float, fftSize * 2>, 4> transform {};
    std::array<float, fftSize> window {};
    std::array<float, imageWidth> pendingDry {}, pendingWet {};
    float windowSum = 0;
    int writePosition = 0, samplesReady = 0;
    double rowPhase = 0, sampleRate = 0;
    std::uint64_t generation = 0, lastSample = 0;

    void clearHistory()
    {
        juce::Graphics g(history);
        g.fillAll(background);
        for (auto& channel : ring) channel.fill(0);
        pendingDry.fill(0);
        pendingWet.fill(0);
        writePosition = samplesReady = 0;
        rowPhase = 0;
    }

    void appendFrame(const SpectrogramStream::Frame& frame)
    {
        if (frame.generation != generation || frame.sampleRate != sampleRate
            || (lastSample != 0 && frame.endSample != lastSample + SpectrogramStream::frameSize))
        {
            clearHistory();
            generation = frame.generation;
            sampleRate = frame.sampleRate;
        }
        lastSample = frame.endSample;
        for (int n = 0; n < SpectrogramStream::frameSize; ++n)
        {
            for (int c = 0; c < 4; ++c) ring[c][writePosition] = frame.samples[c][n];
            writePosition = (writePosition + 1) % fftSize;
        }
        samplesReady = juce::jmin(fftSize, samplesReady + SpectrogramStream::frameSize);
        if (samplesReady < fftSize) return;
        for (int c = 0; c < 4; ++c)
        {
            transform[c].fill(0);
            for (int n = 0; n < fftSize; ++n)
                transform[c][n] = ring[c][(writePosition + n) % fftSize] * window[n];
            fft.performFrequencyOnlyForwardTransform(transform[c].data(), true);
        }
        const auto maxFrequency = juce::jmin(20000.0, sampleRate * 0.49);
        const auto binAt = [&](double x)
        {
            return static_cast<float>(20.0 * std::pow(maxFrequency / 20.0, x / imageWidth) * fftSize / sampleRate);
        };
        const auto magnitude = [&](int channel, float lo, float hi)
        {
            const auto index = juce::jlimit(0, bins-2, static_cast<int>(lo));
            const auto fraction = lo - index;
            auto result = transform[channel][index] * (1-fraction) + transform[channel][index+1] * fraction;
            for (int b = static_cast<int>(std::ceil(lo)); b <= juce::jmin(bins-1, static_cast<int>(hi)); ++b)
                result = juce::jmax(result, transform[channel][b]);
            return result * (2.0f / windowSum);
        };
        for (int x = 0; x < imageWidth; ++x)
        {
            const auto lo = binAt(x), hi = binAt(x+1);
            pendingDry[x] = juce::jmax(pendingDry[x], magnitude(0, lo, hi), magnitude(1, lo, hi));
            pendingWet[x] = juce::jmax(pendingWet[x], magnitude(2, lo, hi), magnitude(3, lo, hi));
        }
        // Fixed six-second history, independent of sample rate and UI timer rate.
        rowPhase += SpectrogramStream::frameSize * imageHeight / (6.0 * sampleRate);
        const auto rows = static_cast<int>(rowPhase);
        if (rows == 0) return;
        rowPhase -= rows;
        history.moveImageSection(0, 0, 0, rows, imageWidth, imageHeight-rows);
        juce::Image::BitmapData pixels(history, juce::Image::BitmapData::writeOnly);
        const auto intensity = [](float amplitude)
        {
            const auto level = juce::jlimit(0.0f, 1.0f,
                (20.0f * std::log10(juce::jmax(1.0e-6f, amplitude)) + 80.0f) / 80.0f);
            return level * level;
        };
        for (int x = 0; x < imageWidth; ++x)
        {
            const auto dry = intensity(pendingDry[x]), wet = intensity(pendingWet[x]);
            const auto channel = [](float value) { return static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f, value)); };
            const juce::Colour colour(channel(12+221*dry+40*wet), channel(18+160*dry+155*wet), channel(23+35*dry+215*wet));
            for (int y = imageHeight-rows; y < imageHeight; ++y) pixels.setPixelColour(x, y, colour);
        }
        pendingDry.fill(0);
        pendingWet.fill(0);
    }

    void timerCallback() override
    {
        SpectrogramStream::Frame frame;
        const auto currentGeneration = processor.spectrogramGeneration();
        if (generation != currentGeneration)
        {
            clearHistory();
            generation = currentGeneration;
            lastSample = 0;
        }
        // Bounded drain: an audio producer must not keep this callback running.
        for (int n = 0; n < 64 && processor.readSpectrogramFrame(frame); ++n)
            if (frame.generation == currentGeneration) appendFrame(frame);
        if (isVisible()) repaint();
    }
};
