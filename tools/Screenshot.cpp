#include "PluginProcessor.h"
#include <iostream>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    DistInterleaveProcessor processor;
    if (argc > 2 && juce::String(argv[2]) == "mono")
    {
        auto layout = processor.getBusesLayout();
        layout.inputBuses.set(0, juce::AudioChannelSet::mono());
        layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
        processor.setBusesLayout(layout);
    }
    processor.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    const auto path = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "screenshot.png");
    juce::FileOutputStream stream(path);
    if (!stream.openedOk()) return 1;
    stream.setPosition(0);
    stream.truncate();
    if (!juce::PNGImageFormat().writeImageToStream(snapshot, stream)) return 1;
    std::cout << path.getFullPathName() << '\n';
}
