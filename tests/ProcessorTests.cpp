#include "PluginProcessor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
void set(DistInterleaveProcessor& p, const char* id, float value)
{
    auto* parameter = p.parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
void layout(DistInterleaveProcessor& p, int main, int side, int out)
{
    auto buses = p.getBusesLayout();
    buses.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(main));
    buses.inputBuses.set(1, juce::AudioChannelSet::canonicalChannelSet(side));
    buses.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(out));
    require(p.setBusesLayout(buses), "Requested mono/stereo bus layout must be supported");
}

std::vector<float> render(int blockSize, int main, int side, int out, int mode, int channels)
{
    DistInterleaveProcessor p;
    layout(p, main, side, out);
    set(p, "mode", static_cast<float>(mode));
    set(p, "channels", static_cast<float>(channels));
    p.prepareToPlay(48000, blockSize);
    std::vector<float> result;
    juce::MidiBuffer midi;
    for (int start = 0; start < 4096; start += blockSize)
    {
        const int count = std::min(blockSize, 4096 - start);
        juce::AudioBuffer<float> block(std::max(main + side, out), count);
        block.clear();
        for (int c = 0; c < main + side; ++c)
            for (int i = 0; i < count; ++i)
                block.setSample(c, i, static_cast<float>(std::sin((start + i) * 0.07 * (c + 1))) * (0.2f + 0.1f * c));
        p.processBlock(block, midi);
        for (int i = 0; i < count; ++i)
            for (int c = 0; c < out; ++c)
            {
                const float value = block.getSample(c, i);
                require(std::isfinite(value), "Output must remain finite");
                if (main == 2 && mode == 0 && out == 2)
                    require(value == block.getSample(0, i), "L/R must be dual mono");
                result.push_back(value);
            }
    }
    return result;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    for (int main : {1, 2})
        for (int side : {0, 1, 2})
            for (int out : {1, 2})
                for (int mode : {0, 1})
                    for (int channels : {0, 1, 2})
                        require(render(1, main, side, out, mode, channels)
                                    == render(127, main, side, out, mode, channels),
                                "All layouts must be independent of host block size");
    require(render(64, 1, 2, 2, 0, 0) == render(64, 1, 2, 2, 1, 0),
            "Mono input must force sidechain even when mode is automated to L/R");

    DistInterleaveProcessor p;
    require(p.parameters.getRawParameterValue("cycles")->load() == 1, "Cycles default");
    require(p.parameters.getRawParameterValue("mix")->load() == 100, "Mix default");
    set(p, "mix", 0);
    set(p, "gain", 2);
    p.prepareToPlay(48000, 32);
    juce::AudioBuffer<float> block(2, 32);
    juce::MidiBuffer midi;
    for (int i = 0; i < 32; ++i) { block.setSample(0, i, 1.5f); block.setSample(1, i, 0.5f); }
    p.processBlock(block, midi);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 32; ++i)
            require(block.getSample(c, i) == 2, "Dry L/R must downmix, apply gain, and never clip");

    set(p, "mode", 1);
    set(p, "mix", 100);
    p.prepareToPlay(48000, 32);
    for (int i = 0; i < 32; ++i) { block.setSample(0, i, 1.5f); block.setSample(1, i, 0.5f); }
    p.processBlock(block, midi);
    require(block.getSample(0, 0) == 3 && block.getSample(1, 0) == 1,
            "Disabled sidechain must pass the main input with output gain");

    set(p, "cycles", 87); set(p, "channels", 2); set(p, "mix", 37);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    DistInterleaveProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const char* id : {"mode", "channels", "cycles", "gain", "mix"})
        require(p.parameters.getRawParameterValue(id)->load() == restored.parameters.getRawParameterValue(id)->load(),
                "All parameters must round-trip through host state");
    const char invalid[] = "invalid state";
    restored.setStateInformation(invalid, sizeof(invalid));
    require(restored.parameters.getRawParameterValue("cycles")->load() == 87, "Invalid state must be ignored");
    std::cout << "Processor tests passed\n";
}
