#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff23201d), kPanel (0xff2f2b27), kInk (0xffeadfca), kAccent (0xffd9822b);
const char* kRowLabels[5] { "Nivel", "Afinar", "Decay", "Tono", "Pan" };
constexpr int kTop = 70, kColW = 74, kPadH = 54, kKnob = 52, kMargin = 16;
}

RitmoEditor::RitmoEditor (RitmoProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    for (int i = 0; i < r78::kNumInst; ++i)
    {
        auto& s = strips[(size_t) i];
        s.pad.setButtonText (juce::String (juce::CharPointer_UTF8 (r78::kInstInfo[(size_t) i].name)));
        s.pad.setColour (juce::TextButton::buttonColourId, kPanel.brighter (0.15f));
        s.pad.setColour (juce::TextButton::textColourOffId, kInk);
        // Clic = golpe normal, Mayús+clic = golpe con acento.
        s.pad.onClick = [this, i] { proc.auditionFromUi (i, juce::ModifierKeys::currentModifiers.isShiftDown()); };
        addAndMakeVisible (s.pad);

        for (int k = 0; k < 5; ++k)
        {
            auto& kn = s.knobs[(size_t) k];
            kn.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            kn.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            kn.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
            kn.setColour (juce::Slider::thumbColourId, kInk);
            kn.setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (kn);
            s.sAtt.push_back (std::make_unique<SliderAttachment> (proc.apvts, RitmoProcessor::pid (i, paramNames[k]), kn));
        }
        s.mute.setColour (juce::ToggleButton::textColourId, kInk);
        addAndMakeVisible (s.mute);
        s.mAtt = std::make_unique<ButtonAttachment> (proc.apvts, RitmoProcessor::pid (i, "mute"), s.mute);
    }

    masterKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterKnob.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    masterKnob.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (masterKnob);
    masterAtt = std::make_unique<SliderAttachment> (proc.apvts, "master", masterKnob);

    setSize (kMargin * 2 + 60 + kColW * r78::kNumInst, kTop + kPadH + 5 * (kKnob + 8) + 50);
}

void RitmoEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kInk);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("RITMO CR-78", kMargin, 14, 400, 34, juce::Justification::left);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (kInk.withAlpha (0.6f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("Clic en un pad para escucharlo · Mayús+clic = acento · también responde a MIDI")),
                kMargin, 44, 700, 18, juce::Justification::left);

    g.setFont (juce::FontOptions (12.0f));
    for (int k = 0; k < 5; ++k)
        g.drawText (kRowLabels[k], kMargin, kTop + kPadH + 8 + k * (kKnob + 8), 56, kKnob, juce::Justification::centredLeft);

    g.drawText ("Master", getWidth() - 140, 14, 70, 34, juce::Justification::centredRight);
}

void RitmoEditor::resized()
{
    for (int i = 0; i < r78::kNumInst; ++i)
    {
        auto& s = strips[(size_t) i];
        const int x = kMargin + 60 + i * kColW;
        s.pad.setBounds (x + 2, kTop, kColW - 4, kPadH - 4);
        for (int k = 0; k < 5; ++k)
            s.knobs[(size_t) k].setBounds (x + (kColW - kKnob) / 2, kTop + kPadH + 8 + k * (kKnob + 8), kKnob, kKnob);
        s.mute.setBounds (x + (kColW - 40) / 2, kTop + kPadH + 8 + 5 * (kKnob + 8), 40, 24);
    }
    masterKnob.setBounds (getWidth() - 64, 8, 48, 48);
}
