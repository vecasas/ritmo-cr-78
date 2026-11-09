#pragma once
#include "PluginProcessor.h"

class Ritmo78Editor : public juce::AudioProcessorEditor
{
public:
    explicit Ritmo78Editor (Ritmo78Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Ritmo78Processor& proc;
    juce::Slider masterKnob;
    juce::AudioProcessorValueTreeState::SliderAttachment masterAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ritmo78Editor)
};
