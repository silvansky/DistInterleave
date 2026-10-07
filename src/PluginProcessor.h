#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "InterleaveEngine.h"

class DistInterleaveProcessor final : public juce::AudioProcessor
{
public:
    DistInterleaveProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DistInterleave"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool isMonoInput() const noexcept { return monoInput.load(); }
    bool usesSidechain() const noexcept { return isMonoInput() || mode->load() >= 0.5f; }
    bool hasSidechain() const noexcept { return sidechainPresent.load(); }
    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* channels = nullptr;
    std::atomic<float>* cycles = nullptr;
    std::atomic<float>* gain = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<bool> monoInput { false }, sidechainPresent { false };
    std::array<interleave::Engine, 2> engines;
    juce::SmoothedValue<float> gainRamp, mixRamp;
    int previousRouting = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistInterleaveProcessor)
};
