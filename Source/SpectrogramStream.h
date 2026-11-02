// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

// One audio-thread producer and one editor-thread consumer. The bounded queue
// drops display frames when full; it never blocks or changes audio processing.
class SpectrogramStream
{
public:
    static constexpr int frameSize = 512;
    struct Frame
    {
        std::array<std::array<float, frameSize>, 4> samples {}; // dry L/R, wet L/R
        double sampleRate = 44100;
        std::uint64_t generation = 0, endSample = 0;
    };

    void setActive(bool active) noexcept
    {
        if (active) invalidate();
        enabled.store(active, std::memory_order_release);
    }
    void invalidate() noexcept { generation.fetch_add(1, std::memory_order_release); }
    std::uint64_t currentGeneration() const noexcept { return generation.load(std::memory_order_acquire); }
    bool beginBlock(double sampleRate) noexcept
    {
        if (!enabled.load(std::memory_order_acquire)) return false;
        const auto epoch = currentGeneration();
        if (staging.generation != epoch || staging.sampleRate != sampleRate)
        {
            staging.generation = epoch;
            staging.sampleRate = sampleRate;
            staging.endSample = 0;
            position = 0;
        }
        return true;
    }
    void push(float dryLeft, float dryRight, float wetLeft, float wetRight) noexcept
    {
        staging.samples[0][position] = dryLeft;
        staging.samples[1][position] = dryRight;
        staging.samples[2][position] = wetLeft;
        staging.samples[3][position] = wetRight;
        if (++position != frameSize) return;
        position = 0;
        staging.endSample += frameSize;
        const auto scope = fifo.write(1);
        if (scope.blockSize1 != 0) frames[static_cast<size_t>(scope.startIndex1)] = staging;
    }
    bool read(Frame& frame) noexcept
    {
        const auto scope = fifo.read(1);
        if (scope.blockSize1 == 0) return false;
        frame = frames[static_cast<size_t>(scope.startIndex1)];
        return true;
    }

private:
    juce::AbstractFifo fifo {64};
    std::vector<Frame> frames = std::vector<Frame>(64);
    Frame staging;
    int position = 0;
    std::atomic<bool> enabled {false};
    std::atomic<std::uint64_t> generation {1};
};
