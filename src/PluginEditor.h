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
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    std::function<juce::String()> audioDeviceName;
private:
    void timerCallback() override;
    void updateRouting();
    void updateStatus();
    DistInterleaveProcessor& effectProcessor;
    VarispeedLookAndFeel look;
    juce::Component surface;
    juce::ComboBox mode, channels;
    FineSlider cycles, gain, mix;
    juce::Label helpMark, contextHelpLabel;
    juce::Component::SafePointer<juce::Component> hoveredHelp;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, channelsAttachment;
    std::unique_ptr<SliderAttachment> cyclesAttachment, gainAttachment, mixAttachment;
    bool lastMono = false, lastSide = false, lastConnected = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistInterleaveEditor)
};
