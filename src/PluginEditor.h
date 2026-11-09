#pragma once
#include "PluginProcessor.h"
#include "StepGrid.h"

// Interfaz provisional (fases 2 y 3): transporte y ritmo arriba, rejilla de pasos
// en el centro y una tira de controles por instrumento abajo.
// La interfaz definitiva llega en la fase 5.
class RitmoEditor : public juce::AudioProcessorEditor
{
public:
    explicit RitmoEditor (RitmoProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Strip
    {
        juce::TextButton pad;
        juce::ToggleButton mute { "M" };
        std::array<juce::Slider, 5> knobs;
        std::vector<std::unique_ptr<SliderAttachment>> sAtt;
        std::unique_ptr<ButtonAttachment> mAtt;
    };

    void setupKnob (juce::Slider&);
    void setupCombo (juce::ComboBox&, const juce::String& paramId);
    void refreshLengthBox();

    RitmoProcessor& proc;
    std::array<Strip, r78::kNumInst> strips;

    // Transporte y secuenciador
    juce::TextButton playBtn { "PLAY" };
    juce::ComboBox rhythmBox, variationBox, fillBox, lengthBox;
    juce::Slider tempoKnob, swingKnob, accentKnob, masterKnob;
    juce::ToggleButton syncBtn { "Sync DAW" };
    std::array<juce::TextButton, 3> varTabs { juce::TextButton { "Editar A" }, juce::TextButton { "Editar B" },
                                              juce::TextButton { "Editar Fill" } };
    juce::TextButton clearBtn { "Borrar" };
    StepGrid grid;

    std::unique_ptr<ButtonAttachment> playAtt, syncAtt;
    std::vector<std::unique_ptr<ComboAttachment>> comboAtts;
    std::vector<std::unique_ptr<SliderAttachment>> knobAtts;

    static constexpr const char* paramNames[5] { "level", "tune", "decay", "tone", "pan" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RitmoEditor)
};
