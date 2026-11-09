#pragma once
#include "PluginProcessor.h"
#include "ui/VintageLookAndFeel.h"
#include "StepGrid.h"
#include "FxPanel.h"
#include "SongStrip.h"
#include "MidiPanel.h"

// Grupo de teclas con LED enlazado a un parámetro de elección (ritmo, variación, fill).
class ChoiceKeys : private juce::Timer
{
public:
    ChoiceKeys (juce::Component& parent, juce::AudioProcessorValueTreeState& s, const juce::String& paramId,
                std::function<juce::Colour (int)> colourFor);
    std::vector<std::unique_ptr<juce::TextButton>> keys;
    std::function<void()> onChange;

private:
    void timerCallback() override;
    juce::RangedAudioParameter* param = nullptr;
    int shown = -1;
};

// Panel principal con todo el contenido a tamaño base; el editor lo escala.
class MainPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kW = 1240, kH = 900;
    explicit MainPanel (RitmoProcessor&);
    ~MainPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showPage (int page);

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Strip
    {
        juce::TextButton pad;
        juce::ToggleButton mute { "Mute" };
        juce::ToggleButton send { "FX" };
        std::array<juce::Slider, 5> knobs;
        std::vector<std::unique_ptr<SliderAttachment>> sAtt;
        std::unique_ptr<ButtonAttachment> mAtt, sendAtt;
    };

    void setupKnob (juce::Slider&, bool big = false);
    void refreshLengthBox();
    void refreshMode();
    void refreshSlotKeys();
    void timerCallback() override;

    RitmoProcessor& proc;
    std::array<Strip, r78::kNumInst> strips;

    juce::TextButton playBtn { "Start / Stop" };
    std::unique_ptr<ChoiceKeys> rhythmKeys, variationKeys, fillKeys, modeKeys;
    std::array<juce::TextButton, RitmoProcessor::kUserSlots> slotKeys;
    SongStrip songStrip;
    int shownMode = -1, shownSlot = -1, slotRefreshTick = 0;
    const r78::Pattern* shownEdit = nullptr;
    // Portapapeles de patrón completo (las tres variaciones y la longitud).
    struct Clip { bool valid = false; int len = 16, spb = 4; bool cells[r78::kNumVars][r78::kRows][r78::kMaxSteps] {}; } clip;
    juce::TextButton copyBtn { "Copiar" }, pasteBtn { "Pegar" };
    std::array<juce::TextButton, r78::kCancelGroups> cancelKeys;
    std::vector<std::unique_ptr<ButtonAttachment>> cancelAtts;
    juce::TextButton fadeInBtn { "Fade in" }, fadeOutBtn { "Fade out" };
    juce::Slider fadeKnob;
    int shownFade = 0;
    juce::ComboBox lengthBox, fillTypeBox;
    std::unique_ptr<ComboAttachment> fillTypeAtt;
    juce::Slider tempoKnob, swingKnob, accentKnob, masterKnob;
    juce::ToggleButton syncBtn { "Sync DAW" };
    std::array<juce::TextButton, 3> varTabs;
    juce::TextButton clearBtn { "Borrar" };
    StepGrid grid;
    FxPanel fxPanel;
    MidiPanel midiPanel;
    std::array<juce::TextButton, 3> pageTabs;
    int page = 0;
    float shownGr = 0.0f;
    bool lastPlaying = false;

    std::unique_ptr<ButtonAttachment> playAtt, syncAtt;
    std::vector<std::unique_ptr<SliderAttachment>> knobAtts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

class RitmoEditor : public juce::AudioProcessorEditor
{
public:
    explicit RitmoEditor (RitmoProcessor&);
    ~RitmoEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    MainPanel& getPanel() { return panel; }

private:
    vintage::LookAndFeel laf;   // debe vivir más que los componentes
    MainPanel panel;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RitmoEditor)
};
