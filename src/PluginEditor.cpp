#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff23201d), kPanel (0xff2f2b27), kInk (0xffeadfca), kAccent (0xffd9822b);
const char* kRowLabels[5] { "Nivel", "Afinar", "Decay", "Tono", "Pan" };
constexpr int kMargin = 14, kHeader = 56, kTransportH = 64, kTabsH = 28;
constexpr int kGridH = r78::kRows * StepGrid::cellH;
constexpr int kColW = 74, kPadH = 40, kKnob = 40;
constexpr int kStripsTop = kHeader + kTransportH + kTabsH + kGridH + 18;
}

void RitmoEditor::setupKnob (juce::Slider& kn)
{
    kn.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    kn.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    kn.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    kn.setColour (juce::Slider::thumbColourId, kInk);
    kn.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (kn);
}

void RitmoEditor::setupCombo (juce::ComboBox& box, const juce::String& paramId)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    comboAtts.push_back (std::make_unique<ComboAttachment> (proc.apvts, paramId, box));
}

void RitmoEditor::refreshLengthBox()
{
    const int len = proc.pattern.length.load();
    const int spb = proc.pattern.stepsPerBeat.load();
    lengthBox.setSelectedId (len * 10 + spb, juce::dontSendNotification);
}

void RitmoEditor::showPage (int newPage)
{
    page = newPage;
    for (auto& st : strips)
    {
        st.pad.setVisible (page == 0); st.mute.setVisible (page == 0); st.send.setVisible (page == 0);
        for (auto& k : st.knobs) k.setVisible (page == 0);
    }
    fxPanel.setVisible (page == 1);
    pageTabs[(size_t) page].setToggleState (true, juce::dontSendNotification);
    repaint();
}

RitmoEditor::RitmoEditor (RitmoProcessor& p) : AudioProcessorEditor (&p), proc (p), grid (p), fxPanel (p)
{
    // ---- Transporte ----
    playBtn.setClickingTogglesState (true);
    playBtn.setColour (juce::TextButton::buttonOnColourId, kAccent);
    addAndMakeVisible (playBtn);
    playAtt = std::make_unique<ButtonAttachment> (proc.apvts, "play", playBtn);

    setupCombo (rhythmBox, "rhythm");
    setupCombo (variationBox, "variation");
    setupCombo (fillBox, "fill");
    rhythmBox.onChange = [this] { juce::Timer::callAfterDelay (120, [safe = juce::Component::SafePointer<RitmoEditor> (this)]
                                                                     { if (safe) { safe->refreshLengthBox(); safe->grid.repaint(); } }); };

    // Longitud del patrón: id = pasos*10 + pasos por pulso
    struct L { int len, spb; const char* name; };
    for (auto l : { L { 8, 4, "8 (2/4)" }, L { 12, 4, "12 (3/4)" }, L { 16, 4, "16 (4/4)" }, L { 24, 4, "24 (6/4)" },
                    L { 32, 4, "32 (2 compases)" }, L { 9, 3, "9 (3/4 tresillos)" }, L { 12, 3, "12 (12/8)" },
                    L { 24, 3, "24 (2 x 12/8)" } })
        lengthBox.addItem (l.name, l.len * 10 + l.spb);
    lengthBox.onChange = [this] {
        const int id = lengthBox.getSelectedId();
        if (id <= 0) return;
        proc.pattern.length.store (id / 10);
        proc.pattern.stepsPerBeat.store (id % 10);
        grid.repaint();
    };
    addAndMakeVisible (lengthBox);
    refreshLengthBox();

    for (auto* k : { &tempoKnob, &swingKnob, &accentKnob, &masterKnob }) setupKnob (*k);
    knobAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, "tempo", tempoKnob));
    knobAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, "swing", swingKnob));
    knobAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, "accent", accentKnob));
    knobAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, "master", masterKnob));

    syncBtn.setColour (juce::ToggleButton::textColourId, kInk);
    addAndMakeVisible (syncBtn);
    syncAtt = std::make_unique<ButtonAttachment> (proc.apvts, "sync", syncBtn);

    // ---- Pestañas de edición A / B / Fill ----
    for (int v = 0; v < 3; ++v)
    {
        auto& t = varTabs[(size_t) v];
        t.setRadioGroupId (77);
        t.setClickingTogglesState (true);
        t.setColour (juce::TextButton::buttonOnColourId, kAccent.darker (0.2f));
        t.onClick = [this, v] { grid.editVar = v; grid.repaint(); };
        addAndMakeVisible (t);
    }
    varTabs[0].setToggleState (true, juce::dontSendNotification);
    clearBtn.onClick = [this] {
        for (int r = 0; r < r78::kRows; ++r)
            for (int s = 0; s < r78::kMaxSteps; ++s) proc.pattern.set (grid.editVar, r, s, false);
        grid.repaint();
    };
    addAndMakeVisible (clearBtn);
    addAndMakeVisible (grid);

    // ---- Tiras por instrumento ----
    for (int i = 0; i < r78::kNumInst; ++i)
    {
        auto& s = strips[(size_t) i];
        s.pad.setButtonText (juce::String (juce::CharPointer_UTF8 (r78::kInstInfo[(size_t) i].name)));
        s.pad.setColour (juce::TextButton::buttonColourId, kPanel.brighter (0.15f));
        s.pad.setColour (juce::TextButton::textColourOffId, kInk);
        s.pad.onClick = [this, i] { proc.auditionFromUi (i, juce::ModifierKeys::currentModifiers.isShiftDown()); };
        addAndMakeVisible (s.pad);
        for (int k = 0; k < 5; ++k)
        {
            setupKnob (s.knobs[(size_t) k]);
            s.sAtt.push_back (std::make_unique<SliderAttachment> (proc.apvts, RitmoProcessor::pid (i, paramNames[k]), s.knobs[(size_t) k]));
        }
        s.mute.setColour (juce::ToggleButton::textColourId, kInk);
        addAndMakeVisible (s.mute);
        s.mAtt = std::make_unique<ButtonAttachment> (proc.apvts, RitmoProcessor::pid (i, "mute"), s.mute);
        s.send.setColour (juce::ToggleButton::textColourId, kInk);
        s.send.setTooltip (juce::String (juce::CharPointer_UTF8 ("Envía este instrumento a los efectos")));
        addAndMakeVisible (s.send);
        s.sendAtt = std::make_unique<ButtonAttachment> (proc.apvts, RitmoProcessor::pid (i, "send"), s.send);
    }

    addChildComponent (fxPanel);
    for (int t = 0; t < 2; ++t)
    {
        auto& b = pageTabs[(size_t) t];
        b.setButtonText (juce::String (juce::CharPointer_UTF8 (t == 0 ? "Voces" : "Efectos y máster")));
        b.setRadioGroupId (88);
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent.darker (0.2f));
        b.onClick = [this, t] { showPage (t); };
        addAndMakeVisible (b);
    }
    showPage (0);

    setSize (juce::jmax (kMargin * 2 + 56 + kColW * r78::kNumInst, fxPanel.preferredWidth() + 2 * kMargin),
             kStripsTop + 30 + kPadH + 5 * (kKnob + 4) + 34);
}

void RitmoEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kInk);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("RITMO CR-78", kMargin, 10, 300, 34, juce::Justification::left);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (kInk.withAlpha (0.6f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("Pads: clic para escuchar, Mayús+clic = acento · Rejilla: clic o arrastrar para programar")),
                kMargin + 220, 18, 640, 18, juce::Justification::left);

    // Etiquetas de transporte
    g.setColour (kInk.withAlpha (0.75f));
    const int ly = kHeader + kTransportH - 14;
    auto label = [&] (juce::Component& c, const char* t) {
        g.drawText (juce::String (juce::CharPointer_UTF8 (t)), c.getX() - 10, ly, c.getWidth() + 20, 14, juce::Justification::centred);
    };
    label (rhythmBox, "Ritmo"); label (variationBox, "Variación"); label (fillBox, "Fill cada");
    label (lengthBox, "Pasos"); label (tempoKnob, "Tempo"); label (swingKnob, "Swing");
    label (accentKnob, "Acento"); label (masterKnob, "Master");

    g.setColour (kPanel);
    g.fillRect (0, kStripsTop - 8, getWidth(), 2);
    g.setColour (kInk.withAlpha (0.75f));
    if (page == 0)
        for (int k = 0; k < 5; ++k)
            g.drawText (kRowLabels[k], kMargin, kStripsTop + 30 + kPadH + 4 + k * (kKnob + 4), 54, kKnob, juce::Justification::centredLeft);
}

void RitmoEditor::resized()
{
    // Transporte
    int x = kMargin;
    const int y = kHeader;
    playBtn.setBounds (x, y + 4, 70, 32); x += 84;
    rhythmBox.setBounds (x, y + 8, 170, 26); x += 184;
    variationBox.setBounds (x, y + 8, 80, 26); x += 94;
    fillBox.setBounds (x, y + 8, 80, 26); x += 94;
    lengthBox.setBounds (x, y + 8, 150, 26); x += 170;
    for (auto* k : { &tempoKnob, &swingKnob, &accentKnob, &masterKnob }) { k->setBounds (x, y - 4, 48, 48); x += 64; }
    syncBtn.setBounds (x + 6, y + 8, 100, 26);

    // Pestañas y rejilla
    const int ty = kHeader + kTransportH;
    for (int v = 0; v < 3; ++v) varTabs[(size_t) v].setBounds (kMargin + StepGrid::labelW + v * 96, ty, 90, 22);
    clearBtn.setBounds (kMargin + StepGrid::labelW + 3 * 96 + 10, ty, 70, 22);
    grid.setBounds (kMargin, ty + kTabsH, getWidth() - 2 * kMargin, kGridH);

    // Pestañas de página
    pageTabs[0].setBounds (kMargin, kStripsTop, 90, 22);
    pageTabs[1].setBounds (kMargin + 96, kStripsTop, 140, 22);
    const int top = kStripsTop + 30;

    // Tiras
    for (int i = 0; i < r78::kNumInst; ++i)
    {
        auto& s = strips[(size_t) i];
        const int sx = kMargin + 56 + i * kColW;
        s.pad.setBounds (sx + 2, top, kColW - 4, kPadH - 4);
        for (int k = 0; k < 5; ++k)
            s.knobs[(size_t) k].setBounds (sx + (kColW - kKnob) / 2, top + kPadH + 4 + k * (kKnob + 4), kKnob, kKnob);
        s.mute.setBounds (sx + 2, top + kPadH + 4 + 5 * (kKnob + 4), 36, 24);
        s.send.setBounds (sx + 36, top + kPadH + 4 + 5 * (kKnob + 4), 38, 24);
    }
    fxPanel.setBounds (kMargin, top, getWidth() - 2 * kMargin, getHeight() - top - kMargin);
}
