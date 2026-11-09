#pragma once
#include "PluginProcessor.h"

// Interfaz provisional de la fase 2: una columna por instrumento con pad de prueba
// y sus cinco controles. La interfaz definitiva llega en la fase 5.
class RitmoEditor : public juce::AudioProcessorEditor
{
public:
    explicit RitmoEditor (RitmoProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Strip
    {
        juce::TextButton pad;
        juce::ToggleButton mute { "M" };
        std::array<juce::Slider, 5> knobs;
        std::vector<std::unique_ptr<SliderAttachment>> sAtt;
        std::unique_ptr<ButtonAttachment> mAtt;
    };

    RitmoProcessor& proc;
    std::array<Strip, r78::kNumInst> strips;
    juce::Slider masterKnob;
    std::unique_ptr<SliderAttachment> masterAtt;

    static constexpr const char* paramNames[5] { "level", "tune", "decay", "tone", "pan" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RitmoEditor)
};
