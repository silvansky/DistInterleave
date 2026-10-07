#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace interleave
{
struct Frame
{
    float left = 0.0f, right = 0.0f;
};

// Independently implemented streaming cycle concatenation. Each source owns
// three preallocated cycle slots: capture, most recently completed, and playback.
// Publishing and acquiring a cycle only exchange indices (no audio-thread copy).
class Engine
{
public:
    void prepare(double sampleRate)
    {
        capacity = std::max(8, static_cast<int>(std::ceil(sampleRate * 2.0)));
        for (auto& source : sources)
            for (auto& slot : source.slots)
                slot.samples.resize(static_cast<size_t>(capacity));
        reset();
    }

    void reset() noexcept
    {
        for (auto& source : sources)
            source.reset();
        active = 0;
        playing = false;
        position = 0;
        playedCycles = 0;
    }

    void setCycles(int value) noexcept
    {
        value = std::clamp(value, 1, 100);
        if (value != cycles)
        {
            cycles = value;
            reset();
        }
    }

    Frame process(Frame a, Frame b, float detectorA, float detectorB) noexcept
    {
        capture(sources[0], a, detectorA);
        capture(sources[1], b, detectorB);
        auto& source = sources[static_cast<size_t>(active)];
        if (!playing)
        {
            if (!source.ready)
                return {};
            std::swap(source.playSlot, source.readySlot);
            source.ready = false;
            position = 0;
            playing = true;
        }
        const auto& slot = source.slots[static_cast<size_t>(source.playSlot)];
        const auto result = slot.samples[static_cast<size_t>(position++)];
        if (position == slot.length)
        {
            playing = false;
            if (++playedCycles >= cycles)
            {
                playedCycles = 0;
                active = 1 - active;
            }
        }
        return result;
    }

private:
    struct Slot { std::vector<Frame> samples; int length = 0; };
    struct Source
    {
        std::array<Slot, 3> slots;
        int captureSlot = 0, readySlot = 1, playSlot = 2;
        int lastSign = 0;
        bool aligned = false, ready = false;
        void reset() noexcept
        {
            captureSlot = 0; readySlot = 1; playSlot = 2;
            lastSign = 0;
            aligned = ready = false;
            for (auto& slot : slots) slot.length = 0;
        }
    };

    void publish(Source& source) noexcept
    {
        std::swap(source.captureSlot, source.readySlot);
        source.ready = true;
        source.slots[static_cast<size_t>(source.captureSlot)].length = 0;
    }

    void capture(Source& source, Frame frame, float detector) noexcept
    {
        // Exact zero holds the previous sign, so zero plateaus cannot create
        // phantom cycles. Two sign changes give a rising-to-rising full cycle.
        const int sign = detector > 0.0f ? 1 : (detector < 0.0f ? -1 : 0);
        const bool rising = sign > 0 && source.lastSign < 0;
        if (sign != 0) source.lastSign = sign;
        if (rising)
        {
            if (!source.aligned)
            {
                source.slots[static_cast<size_t>(source.captureSlot)].length = 0;
                source.aligned = true;
            }
            else
                publish(source);
        }

        auto& slot = source.slots[static_cast<size_t>(source.captureSlot)];
        slot.samples[static_cast<size_t>(slot.length++)] = frame;
        // DC, silence, or a very long cycle must not stall or grow memory forever.
        if (slot.length == capacity)
        {
            publish(source);
            source.aligned = false;
        }
    }

    std::array<Source, 2> sources;
    int capacity = 0, cycles = 1, active = 0, position = 0, playedCycles = 0;
    bool playing = false;
};
}
