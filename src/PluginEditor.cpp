#include "PluginEditor.h"

Ritmo78Editor::Ritmo78Editor (Ritmo78Processor& p)
    : AudioProcessorEditor (&p), proc (p),
      masterAttach (p.apvts, "master", masterKnob)
{
    masterKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
    addAndMakeVisible (masterKnob);
    setSize (720, 360);
}

void Ritmo78Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1c1a));
    g.setColour (juce::Colour (0xffe8dcc8));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("RITMO CR-78", getLocalBounds().removeFromTop (80), juce::Justification::centred);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Fase 1: esqueleto. Toca cualquier nota MIDI.", getLocalBounds().removeFromBottom (40),
                juce::Justification::centred);
}

void Ritmo78Editor::resized()
{
    masterKnob.setBounds (getLocalBounds().withSizeKeepingCentre (110, 130));
}
