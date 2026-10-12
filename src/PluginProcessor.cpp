#include "PluginProcessor.h"
#include "PluginEditor.h"

DistInterleaveProcessor::DistInterleaveProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "DistInterleaveState", makeParameters())
{
    mode = parameters.getRawParameterValue("mode");
    channels = parameters.getRawParameterValue("channels");
    cycles = parameters.getRawParameterValue("cycles");
    gain = parameters.getRawParameterValue("gain");
    mix = parameters.getRawParameterValue("mix");
    threshold = parameters.getRawParameterValue("threshold");
    silenceMode = parameters.getRawParameterValue("silenceMode");
}

juce::AudioProcessorValueTreeState::ParameterLayout DistInterleaveProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode", 1}, "Mode",
        juce::StringArray{"L/R", "In/Sidechain"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"channels", 1}, "Channels",
        juce::StringArray{"Independent", "Left", "Right"}, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"cycles", 1}, "Cycles", 1, 100, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"gain", 1}, "Gain",
        juce::NormalisableRange<float>{0.0f, 2.0f, 0.001f}, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix", 1}, "Mix",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.01f}, 100.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"threshold", 2}, "Silence Threshold",
        juce::NormalisableRange<float>{-120.0f, -20.0f, 0.1f}, -90.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"silenceMode", 2}, "Silence Mode",
        juce::StringArray{"Mute", "Bypass"}, 0));
    return layout;
}

bool DistInterleaveProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto supported = [](const juce::AudioChannelSet& set)
    { return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo(); };
    return layout.inputBuses.size() == 2 && layout.outputBuses.size() == 1
        && supported(layout.getMainInputChannelSet()) && supported(layout.getMainOutputChannelSet())
        && (layout.inputBuses[1].isDisabled() || supported(layout.inputBuses[1]));
}

void DistInterleaveProcessor::prepareToPlay(double rate, int)
{
    for (auto& engine : engines) engine.prepare(rate);
    gainRamp.reset(rate, 0.01);
    mixRamp.reset(rate, 0.01);
    gainRamp.setCurrentAndTargetValue(gain->load());
    mixRamp.setCurrentAndTargetValue(mix->load() * 0.01f);
    monoInput.store(getMainBusNumInputChannels() == 1);
    sidechainPresent.store(getChannelCountOfBus(true, 1) > 0);
    previousRouting = -1;
    silenceHoldSamples = std::max(1, static_cast<int>(std::ceil(rate * 0.05)));
    activityHold = {};
    previousActivity = -1;
    setLatencySamples(0); // Content-dependent capture delay cannot be represented by fixed PDC.
}

void DistInterleaveProcessor::reset()
{
    for (auto& engine : engines) engine.reset();
    previousRouting = -1;
    activityHold = {};
    previousActivity = -1;
}

void DistInterleaveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    auto input = getBusBuffer(buffer, true, 0);
    auto output = getBusBuffer(buffer, false, 0);
    auto side = getBusBuffer(buffer, true, 1);
    const bool mono = input.getNumChannels() == 1;
    const bool useSide = mono || mode->load() >= 0.5f;
    const int detection = juce::jlimit(0, 2, juce::roundToInt(channels->load()));
    const bool sideAvailable = side.getNumChannels() > 0;
    monoInput.store(mono);
    sidechainPresent.store(sideAvailable);
    const int routing = (useSide ? 1 : 0) + detection * 2 + (mono ? 8 : 0)
                      + side.getNumChannels() * 16 + output.getNumChannels() * 64;
    if (routing != previousRouting)
    {
        for (auto& engine : engines) engine.reset();
        activityHold = {};
        previousActivity = -1;
        previousRouting = routing;
    }
    for (auto& engine : engines) engine.setCycles(juce::roundToInt(cycles->load()));
    gainRamp.setTargetValue(gain->load());
    mixRamp.setTargetValue(mix->load() * 0.01f);
    const float silenceThreshold = juce::Decibels::decibelsToGain(threshold->load(), -160.0f);
    const bool bypassSilence = silenceMode->load() >= 0.5f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // Read every input before writing: a mono main bus can alias output R
        // with the first sidechain channel in JUCE's shared process buffer.
        const interleave::Frame a {input.getSample(0, i), input.getSample(mono ? 0 : 1, i)};
        const interleave::Frame b = sideAvailable
            ? interleave::Frame{side.getSample(0, i), side.getSample(side.getNumChannels() == 1 ? 0 : 1, i)}
            : interleave::Frame{};
        interleave::Frame dry = a, wet;
        // Detect whole sources independently of the cycle-boundary channel.
        // A hold avoids gating normal zero crossings and is block-size independent.
        const std::array<float, 2> levels = useSide
            ? std::array<float, 2>{std::max(std::abs(a.left), std::abs(a.right)),
                                   std::max(std::abs(b.left), std::abs(b.right))}
            : std::array<float, 2>{std::abs(a.left), std::abs(a.right)};
        int activity = 0;
        for (size_t source = 0; source < activityHold.size(); ++source)
        {
            if (levels[source] >= silenceThreshold)
                activityHold[source] = silenceHoldSamples;
            else if (activityHold[source] > 0)
                --activityHold[source];
            if (activityHold[source] > 0) activity |= 1 << source;
        }
        if (activity != previousActivity)
        {
            // Never resume a captured cycle from before a silent interval.
            for (auto& engine : engines) engine.reset();
            previousActivity = activity;
        }
        const bool silenceOverride = (!useSide || sideAvailable) && activity != 3;
        if (silenceOverride)
        {
            if (bypassSilence && activity != 0)
                wet = useSide ? (activity == 1 ? a : b)
                              : interleave::Frame{activity == 1 ? a.left : a.right,
                                                  activity == 1 ? a.left : a.right};
            // Override the complete dry/wet blend, including Mix = 0.
            dry = wet;
        }
        else if (!useSide)
        {
            dry.left = dry.right = (a.left + a.right) * 0.5f;
            wet = engines[0].process({a.left, a.left}, {a.right, a.right}, a.left, a.right);
        }
        else if (!sideAvailable)
            wet = a;
        else if (detection == 0)
        {
            wet.left = engines[0].process({a.left, a.left}, {b.left, b.left}, a.left, b.left).left;
            wet.right = engines[1].process({a.right, a.right}, {b.right, b.right}, a.right, b.right).left;
        }
        else
            wet = engines[0].process(a, b, detection == 1 ? a.left : a.right,
                                           detection == 1 ? b.left : b.right);

        const float wetAmount = mixRamp.getNextValue(), outputGain = gainRamp.getNextValue();
        const float left = (dry.left + wetAmount * (wet.left - dry.left)) * outputGain;
        const float right = (dry.right + wetAmount * (wet.right - dry.right)) * outputGain;
        output.setSample(0, i, output.getNumChannels() == 1 ? (left + right) * 0.5f : left);
        if (output.getNumChannels() == 2) output.setSample(1, i, right);
    }
}

void DistInterleaveProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = parameters.copyState().createXml()) copyXmlToBinary(*xml, dest);
}

void DistInterleaveProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName(parameters.state.getType()))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        // Older sessions omit these parameters. Restore their defaults even if
        // this instance has already loaded a newer preset.
        for (const auto* id : {"threshold", "silenceMode"})
            if (!state.getChildWithProperty("id", id).isValid())
            {
                auto* parameter = parameters.getParameter(id);
                juce::ValueTree value("PARAM");
                value.setProperty("id", id, nullptr);
                value.setProperty("value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
                state.appendChild(value, nullptr);
            }
        parameters.replaceState(state);
    }
}

juce::AudioProcessorEditor* DistInterleaveProcessor::createEditor() { return new DistInterleaveEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DistInterleaveProcessor(); }
