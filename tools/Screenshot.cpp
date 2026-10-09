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
    processor.setRateAndBufferSizeDetails(48000, 512);
    processor.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setVisible(true);
    // Optional design-space hover coordinates let previews include contextual help.
    if (argc > 4)
    {
        const juce::Point<int> position {juce::String(argv[3]).getIntValue(), juce::String(argv[4]).getIntValue()};
        auto* component = editor->getComponentAt(position);
        if (component == nullptr) return 1;
        const auto localPosition = component->getLocalPoint(editor.get(), position).toFloat();
        const auto now = juce::Time::getCurrentTime();
        editor->mouseEnter(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
            localPosition, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, component, component,
            now, localPosition, now, 0, false));
    }
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    const auto path = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "screenshot.png");
    juce::FileOutputStream stream(path);
    if (!stream.openedOk()) return 1;
    stream.setPosition(0);
    stream.truncate();
    if (!juce::PNGImageFormat().writeImageToStream(snapshot, stream)) return 1;
    std::cout << path.getFullPathName() << '\n';
}
