#include "PluginEditor.h"

using namespace vspd;
namespace
{
constexpr int designWidth = 660, designHeight = 330;
void caption(juce::Graphics& g, const juce::String& text, float x, float y)
{
    g.setFont(uiFont(9.5f));
    g.setColour(juce::Colour(col::mid));
    g.drawSingleLineText(text, juce::roundToInt(x), juce::roundToInt(y), juce::Justification::horizontallyCentred);
}
}

DistInterleaveEditor::DistInterleaveEditor(DistInterleaveProcessor& p)
    : AudioProcessorEditor(p), effectProcessor(p)
{
    setLookAndFeel(&look);
    addAndMakeVisible(surface);
    surface.setInterceptsMouseClicks(false, true);
    for (auto* component : std::initializer_list<juce::Component*>{&mode, &channels, &cycles, &gain, &mix})
        surface.addAndMakeVisible(component);
    mode.addItemList({"L/R", "In/Sidechain"}, 1);
    channels.addItemList({"Independent", "Left", "Right"}, 1);
    mode.setTooltip("L/R interleaves the input channels into mono. In/Sidechain interleaves the main and sidechain buses.");
    channels.setTooltip("Independent detects each channel separately. Left or Right uses that channel's boundaries for the stereo pair.");
    const auto setupKnob = [](juce::Slider& slider, double initial, const juce::String& help)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters(kRotaryStart, kRotaryEnd, true);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 112, 26);
        slider.setDoubleClickReturnValue(true, initial);
        slider.setTooltip(help);
    };
    setupKnob(cycles, 1.0, "Consecutive full cycles from each source before switching. Range 1-100.");
    setupKnob(gain, 1.0, "Linear output gain after the dry/wet blend. 0-2x; no limiter or clipping stage.");
    setupKnob(mix, 100.0, "Dry/wet blend. L/R dry is the mono average of both inputs.");
    modeAttachment = std::make_unique<ComboAttachment>(p.parameters, "mode", mode);
    channelsAttachment = std::make_unique<ComboAttachment>(p.parameters, "channels", channels);
    cyclesAttachment = std::make_unique<SliderAttachment>(p.parameters, "cycles", cycles);
    gainAttachment = std::make_unique<SliderAttachment>(p.parameters, "gain", gain);
    mixAttachment = std::make_unique<SliderAttachment>(p.parameters, "mix", mix);
    cycles.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)); };
    gain.textFromValueFunction = [](double v) { return juce::String(v, 2) + " x"; };
    mix.textFromValueFunction = [](double v) { return juce::String(v, 1) + " %"; };
    for (auto* slider : {&cycles, &gain, &mix})
    {
        slider->valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
        slider->updateText();
    }
    mode.onChange = [this] { updateRouting(); repaint(); };
    setResizable(true, true);
    setResizeLimits(495, 248, 1320, 660);
    getConstrainer()->setFixedAspectRatio(2.0);
    setSize(designWidth, designHeight);
    updateRouting();
    startTimerHz(15);
}

DistInterleaveEditor::~DistInterleaveEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DistInterleaveEditor::resized()
{
    surface.setBounds(0, 0, designWidth, designHeight);
    surface.setTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / designWidth));
    mode.setBounds(24, 116, 186, 28);
    channels.setBounds(24, 192, 186, 28);
    cycles.setBounds(254, 112, 112, 142);
    gain.setBounds(392, 112, 112, 142);
    mix.setBounds(530, 112, 112, 142);
}

void DistInterleaveEditor::updateRouting()
{
    const bool mono = effectProcessor.isMonoInput();
    mode.setItemEnabled(1, !mono);
    mode.setEnabled(!mono);
    if (mono) mode.setSelectedId(2, juce::dontSendNotification);
    else mode.setSelectedId(juce::roundToInt(effectProcessor.parameters.getRawParameterValue("mode")->load()) + 1,
                            juce::dontSendNotification);
    channels.setEnabled(effectProcessor.usesSidechain());
    lastMono = mono;
    lastSide = effectProcessor.usesSidechain();
    lastConnected = effectProcessor.hasSidechain();
}

void DistInterleaveEditor::timerCallback()
{
    // Also correct the visible mode when host automation changes a disabled
    // mode parameter on a mono instance: the effective mode remains sidechain.
    if (lastMono != effectProcessor.isMonoInput() || lastSide != effectProcessor.usesSidechain()
        || lastConnected != effectProcessor.hasSidechain() || (lastMono && mode.getSelectedId() != 2))
    {
        updateRouting();
        repaint();
    }
}

void DistInterleaveEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(col::background));
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / designWidth));
    g.setColour(juce::Colour(col::mid));
    drawTracked(g, uiFont(10.0f, true), "DISTORT : INTERLEAVE", 24.0f, 25.0f, 3.0f);
    g.setFont(monoFont(8.0f));
    g.setColour(juce::Colour(col::dim));
    g.drawText("v0.1.0", 550, 11, 86, 20, juce::Justification::centredRight);
    g.setColour(juce::Colour(col::divider).withAlpha(0.6f));
    g.drawHorizontalLine(38, 0.0f, 660.0f);
    g.drawHorizontalLine(296, 0.0f, 660.0f);
    g.drawVerticalLine(234, 52.0f, 282.0f);
    g.drawVerticalLine(380, 52.0f, 282.0f);
    g.setColour(juce::Colour(col::heading));
    drawTrackedCentred(g, uiFont(14.0f), "SOURCES", 117.0f, 77.0f, 2.6f);
    drawTrackedCentred(g, uiFont(14.0f), "CYCLES", 307.0f, 77.0f, 2.6f);
    drawTrackedCentred(g, uiFont(14.0f), "OUTPUT", 517.0f, 77.0f, 2.6f);
    caption(g, "Mode", 117.0f, 107.0f);
    caption(g, "Channels", 117.0f, 183.0f);
    caption(g, "per source", 310.0f, 107.0f);
    caption(g, "Gain", 448.0f, 107.0f);
    caption(g, "Mix", 586.0f, 107.0f);
    g.setFont(monoFont(7.5f));
    g.setColour(juce::Colour(col::dim));
    g.drawText(lastSide ? (lastMono ? "mono input / sidechain" : "main input / sidechain")
                        : "left / right -> mono", 20, 235, 194, 18, juce::Justification::centred);
    if (lastSide && !lastConnected)
    {
        g.setColour(juce::Colour(col::warn));
        g.drawText("connect sidechain / dry through", 16, 257, 202, 17, juce::Justification::centred);
    }
    else
        g.drawText(lastSide ? "zero-crossing detection" : "channels fixed in L/R mode",
                   20, 257, 194, 17, juce::Justification::centred);
    g.setColour(juce::Colour(col::dim));
    g.drawText("1 - 100", 254, 262, 112, 18, juce::Justification::centred);
    g.drawText("0 - 2x", 392, 262, 112, 18, juce::Justification::centred);
    g.drawText("dry / wet", 530, 262, 112, 18, juce::Justification::centred);
    g.setFont(monoFont(8.0f));
    g.drawText("shift-drag fine  .  double-click reset  .  type values  .  hover for help",
               24, 300, 612, 24, juce::Justification::centredLeft);
}
