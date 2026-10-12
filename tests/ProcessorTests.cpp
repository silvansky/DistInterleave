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

void checkSilenceRouting()
{
    for (int main : {1, 2})
        for (int side : {1, 2})
            for (int out : {1, 2})
                for (int mode : {0, 1})
                    for (int channels : {0, 1, 2})
                        for (int active : {0, 1, 2})
                            for (int silenceMode : {0, 1})
                                for (float mix : {0.0f, 37.0f, 100.0f})
                                {
                                    DistInterleaveProcessor p;
                                    layout(p, main, side, out);
                                    set(p, "mode", static_cast<float>(mode));
                                    set(p, "channels", static_cast<float>(channels));
                                    set(p, "threshold", -40);
                                    set(p, "silenceMode", static_cast<float>(silenceMode));
                                    set(p, "mix", mix);
                                    set(p, "gain", 2);
                                    p.prepareToPlay(1000, 16);
                                    const bool useSide = main == 1 || mode == 1;
                                    juce::AudioBuffer<float> block(std::max(main + side, out), 16);
                                    // Below-threshold noise must count as silence. Stereo
                                    // sources have only their right channel above threshold.
                                    for (int c = 0; c < block.getNumChannels(); ++c)
                                        for (int i = 0; i < 16; ++i) block.setSample(c, i, 0.005f);
                                    if (active != 0)
                                    {
                                        const int channel = useSide
                                            ? (active == 1 ? main - 1 : main + side - 1) : active - 1;
                                        for (int i = 0; i < 16; ++i) block.setSample(channel, i, 0.25f);
                                    }
                                    juce::MidiBuffer midi;
                                    p.processBlock(block, midi);
                                    float left = 0, right = 0;
                                    if (silenceMode == 1 && active != 0)
                                    {
                                        const int sourceChannels = active == 1 ? main : side;
                                        left = useSide && sourceChannels == 2 ? 0.01f : 0.5f;
                                        right = 0.5f;
                                    }
                                    for (int i = 0; i < 16; ++i)
                                    {
                                        require(std::abs(block.getSample(0, i) - (out == 1 ? (left + right) * 0.5f : left)) < 1.0e-6f,
                                                "Silence action must select the active source, override Mix, and apply Gain");
                                        if (out == 2)
                                            require(std::abs(block.getSample(1, i) - right) < 1.0e-6f,
                                                    "Silence routing must preserve stereo and safely read aliased sidechain input");
                                    }
                                }
}

std::vector<float> renderSilenceTransitions(int blockSize, int silenceMode, int rate)
{
    DistInterleaveProcessor p;
    layout(p, 1, 1, 1);
    set(p, "threshold", -40);
    set(p, "silenceMode", static_cast<float>(silenceMode));
    p.prepareToPlay(rate, blockSize);
    std::vector<float> result;
    juce::MidiBuffer midi;
    const int hold = rate / 20;
    for (int start = 0; start < 1200; start += blockSize)
    {
        const int count = std::min(blockSize, 1200 - start);
        juce::AudioBuffer<float> block(2, count);
        for (int i = 0; i < count; ++i)
        {
            const int t = start + i;
            const float a = (t >= 600 && t < 800) || (t >= 900 && t < 1000)
                ? 0.005f : (t % 8 < 4 ? 0.25f : -0.25f);
            const float b = (t >= 200 && t < 400) || (t >= 900 && t < 1000)
                ? 0.005f : (t % 12 < 6 ? 0.5f : -0.5f);
            block.setSample(0, i, a);
            block.setSample(1, i, b);
        }
        p.processBlock(block, midi);
        for (int i = 0; i < count; ++i) result.push_back(block.getSample(0, i));
    }
    for (int t = 200 + hold - 1; t < 400; ++t)
        require(result[static_cast<size_t>(t)] == (silenceMode == 1 ? (t % 8 < 4 ? 0.25f : -0.25f) : 0),
                "A silent sidechain must trigger the selected action after exactly 50 ms");
    for (int t = 600 + hold - 1; t < 800; ++t)
        require(result[static_cast<size_t>(t)] == (silenceMode == 1 ? (t % 12 < 6 ? 0.5f : -0.5f) : 0),
                "A silent main input must bypass the sidechain or mute");
    for (int t = 900 + hold - 1; t < 1000; ++t)
        require(result[static_cast<size_t>(t)] == 0, "Two silent sources must mute in either mode");
    for (int t = 400; t < 408; ++t)
        require(result[static_cast<size_t>(t)] == 0, "Recovery must discard stale cycles and capture fresh boundaries");
    require(result[420] != 0, "Interleaving must resume after fresh cycles arrive");
    return result;
}

void checkSilenceHold()
{
    DistInterleaveProcessor p;
    layout(p, 1, 1, 1);
    set(p, "mix", 0);
    set(p, "threshold", -40);
    p.prepareToPlay(1000, 64);
    juce::AudioBuffer<float> block(2, 64);
    for (int i = 0; i < 64; ++i)
    {
        block.setSample(0, i, 0.25f);
        block.setSample(1, i, i == 0 ? 0.5f : 0.005f);
    }
    juce::MidiBuffer midi;
    p.processBlock(block, midi);
    for (int i = 0; i < 64; ++i)
        require(block.getSample(0, i) == (i < 50 ? 0.25f : 0.0f),
                "Below-threshold intervals shorter than 50 ms must not trigger silence handling");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    checkSilenceRouting();
    checkSilenceHold();
    for (int silenceMode : {0, 1})
        for (int rate : {1000, 2000})
            require(renderSilenceTransitions(1, silenceMode, rate) == renderSilenceTransitions(127, silenceMode, rate),
                    "Silence transitions must be independent of block size at each sample rate");
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
    require(p.parameters.getRawParameterValue("threshold")->load() == -90, "Silence threshold default");
    require(p.parameters.getRawParameterValue("silenceMode")->load() == 0, "Silence mode defaults to Mute");
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
    set(p, "threshold", -54); set(p, "silenceMode", 1);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    DistInterleaveProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const char* id : {"mode", "channels", "cycles", "gain", "mix", "threshold", "silenceMode"})
        require(p.parameters.getRawParameterValue(id)->load() == restored.parameters.getRawParameterValue(id)->load(),
                "All parameters must round-trip through host state");
    const char invalid[] = "invalid state";
    restored.setStateInformation(invalid, sizeof(invalid));
    require(restored.parameters.getRawParameterValue("cycles")->load() == 87, "Invalid state must be ignored");
    auto legacy = p.parameters.copyState();
    legacy.removeChild(legacy.getChildWithProperty("id", "threshold"), nullptr);
    legacy.removeChild(legacy.getChildWithProperty("id", "silenceMode"), nullptr);
    juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), state);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.parameters.getRawParameterValue("threshold")->load() == -90
            && restored.parameters.getRawParameterValue("silenceMode")->load() == 0,
            "Legacy sessions must load the new defaults even over a previously loaded state");
    require(restored.parameters.getRawParameterValue("cycles")->load() == 87, "Legacy parameters must be preserved");
    std::cout << "Processor tests passed\n";
}
