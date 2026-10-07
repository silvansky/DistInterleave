#pragma once
#include "PluginProcessor.h"
#include "LookAndFeel.h"

class FineSlider final : public juce::Slider
{
public:
    void mouseDown(const juce::MouseEvent& e) override
    {
        setMouseDragSensitivity(e.mods.isShiftDown() ? 1800 : 250);
        juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        setMouseDragSensitivity(e.mods.isShiftDown() ? 1800 : 250);
        juce::Slider::mouseDrag(e);
    }
};

class DistInterleaveEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DistInterleaveEditor(DistInterleaveProcessor&);
    ~DistInterleaveEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void updateRouting();
    DistInterleaveProcessor& effectProcessor;
    VarispeedLookAndFeel look;
    juce::Component surface;
    juce::ComboBox mode, channels;
    FineSlider cycles, gain, mix;
    juce::TooltipWindow tooltip {this, 600};
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, channelsAttachment;
    std::unique_ptr<SliderAttachment> cyclesAttachment, gainAttachment, mixAttachment;
    bool lastMono = false, lastSide = false, lastConnected = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistInterleaveEditor)
};
