#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "LookAndFeel.h"

class StandaloneContent final : public juce::Component, private juce::AudioIODeviceCallback
{
public:
    explicit StandaloneContent(bool openAudioDevice)
    {
        setLookAndFeel(&look);
        addAndMakeVisible(settings);
        settings.setButtonText("Audio settings");
        settings.onClick = [this]
        {
            if (audioDialog != nullptr)
            {
                audioDialog->toFront(true);
                return;
            }
            juce::DialogWindow::LaunchOptions options;
            auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(devices, 0, 4, 1, 2, false, false, true, false);
            selector->setSize(520, 420);
            options.content.setOwned(selector.release());
            options.dialogTitle = "DistInterleave audio settings";
            options.dialogBackgroundColour = juce::Colour(vspd::col::background);
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            audioDialog = options.launchAsync();
            audioDialog->setLookAndFeel(&look);
        };
        addAndMakeVisible(routing);
        routing.addItem("Stereo L/R (inputs 1-2)", 1);
        routing.addItem("Mono + mono SC (1 / 2)", 2);
        routing.addItem("Mono + stereo SC (1 / 2-3)", 3);
        routing.addItem("Stereo + mono SC (1-2 / 3)", 4);
        routing.addItem("Stereo + stereo SC (1-2 / 3-4)", 5);
        routing.setSelectedId(1, juce::dontSendNotification);
        routing.onChange = [this] { configureRouting(); };
        addAndMakeVisible(outputs);
        outputs.addItem("Mono out", 1);
        outputs.addItem("Stereo out", 2);
        outputs.setSelectedId(2, juce::dontSendNotification);
        outputs.onChange = [this] { configureRouting(); };
        auto* pluginEditor = new DistInterleaveEditor(processor);
        pluginEditor->audioDeviceName = [this]
        {
            if (auto* device = devices.getCurrentAudioDevice()) return device->getName();
            return juce::String();
        };
        editor.reset(pluginEditor);
        addAndMakeVisible(*editor);
        editor->setResizable(false, false);
        addAndMakeVisible(status);
        status.setFont(vspd::monoFont(8.0f));
        status.setColour(juce::Label::textColourId, juce::Colour(vspd::col::dim));
        status.setText("Hardware inputs only. Enable the channels you need in Audio settings.", juce::dontSendNotification);
        setSize(660, 400);
        if (openAudioDevice)
        {
            const auto error = devices.initialiseWithDefaultDevices(2, 2);
            if (error.isNotEmpty()) status.setText(error, juce::dontSendNotification);
            devices.addAudioCallback(this);
        }
    }

    ~StandaloneContent() override
    {
        if (audioDialog != nullptr) delete audioDialog.getComponent();
        devices.removeAudioCallback(this);
        devices.closeAudioDevice();
        editor.reset();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(vspd::col::background)); }
    void resized() override
    {
        settings.setBounds(16, 10, 110, 26);
        routing.setBounds(136, 10, 360, 26);
        outputs.setBounds(506, 10, 138, 26);
        editor->setBounds(0, 44, 660, 330);
        status.setBounds(16, 374, 628, 22);
    }

private:
    void configureRouting()
    {
        devices.removeAudioCallback(this);
        auto layout = processor.getBusesLayout();
        const int choice = routing.getSelectedId();
        const int mainCount = choice == 2 || choice == 3 ? 1 : 2;
        const int sideCount = choice == 1 ? 0 : (choice == 3 || choice == 5 ? 2 : 1);
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(mainCount));
        layout.inputBuses.set(1, juce::AudioChannelSet::canonicalChannelSet(sideCount));
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(outputs.getSelectedId()));
        processor.setBusesLayout(layout);
        auto* mode = processor.parameters.getParameter("mode");
        mode->setValueNotifyingHost(sideCount > 0 ? 1.0f : 0.0f);
        status.setText("Main: first " + juce::String(mainCount) + " input(s). Sidechain: next "
                       + juce::String(sideCount) + ". Unavailable channels are silent.", juce::dontSendNotification);
        devices.addAudioCallback(this);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        blockSize = juce::jmax(1, device->getCurrentBufferSizeSamples());
        scratch.setSize(juce::jmax(processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels()), blockSize);
        processor.setRateAndBufferSizeDetails(device->getCurrentSampleRate(), blockSize);
        processor.prepareToPlay(device->getCurrentSampleRate(), blockSize);
    }
    void audioDeviceStopped() override { processor.releaseResources(); }
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
        float* const* output, int numOutputs, int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        // Chunk unusually large callbacks instead of allocating on the audio thread.
        for (int offset = 0; offset < numSamples; offset += blockSize)
        {
            const int count = juce::jmin(blockSize, numSamples - offset);
            juce::AudioBuffer<float> block(scratch.getArrayOfWritePointers(), scratch.getNumChannels(), count);
            block.clear();
            for (int channel = 0; channel < juce::jmin(numInputs, processor.getTotalNumInputChannels()); ++channel)
                if (inputs[channel]) block.copyFrom(channel, 0, inputs[channel] + offset, count);
            midi.clear();
            processor.processBlock(block, midi);
            for (int channel = 0; channel < numOutputs; ++channel)
                if (output[channel])
                    juce::FloatVectorOperations::copy(output[channel] + offset,
                        block.getReadPointer(juce::jmin(channel, processor.getTotalNumOutputChannels() - 1)), count);
        }
    }
    VarispeedLookAndFeel look;
    DistInterleaveProcessor processor;
    juce::AudioDeviceManager devices;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::TextButton settings;
    juce::ComboBox routing, outputs;
    juce::Label status;
    juce::Component::SafePointer<juce::DialogWindow> audioDialog;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    int blockSize = 512;
};

class DistInterleaveApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DistInterleave"; }
    const juce::String getApplicationVersion() override { return DISTINTERLEAVE_VERSION; }
    void initialise(const juce::String& arguments) override
    {
        window = std::make_unique<Window>(getApplicationName(), juce::Colour(vspd::col::background),
                                          juce::DocumentWindow::allButtons);
        window->setUsingNativeTitleBar(true);
        const bool smokeTest = arguments.contains("--smoke-test");
        window->setContentOwned(new StandaloneContent(!smokeTest), true);
        window->centreWithSize(window->getWidth(), window->getHeight());
        window->setVisible(true);
        if (smokeTest) juce::Timer::callAfterDelay(300, [] { juce::JUCEApplication::getInstance()->quit(); });
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        using DocumentWindow::DocumentWindow;
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<juce::DocumentWindow> window;
};

START_JUCE_APPLICATION(DistInterleaveApplication)
