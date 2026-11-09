#include "PluginEditor.h"

using namespace vintage;

namespace
{
constexpr int kCheek = 26;                    // laterales de madera
constexpr int kHeaderH = 62;
constexpr int kRowA = 74;                     // ritmos y transporte
constexpr int kGridLabel = 276, kGridTabs = 294, kGridTop = 322;
constexpr int kGridH = r78::kRows * StepGrid::cellH;
constexpr int kPageTabs = kGridTop + kGridH + 14;
constexpr int kPageTop = kPageTabs + 32;
constexpr int kStripW = 84;
const char* kKnobLabels[5] { "Nivel", "Afinar", "Decay", "Tono", "Pan" };

juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// Color de las teclas de ritmo según el estilo (familias con colores de época).
juce::Colour styleColour (const char* style)
{
    const juce::String s (style);
    if (s == "Rock")     return brick;
    if (s == "New Wave") return orange;
    if (s == "Electro")  return juce::Colour (0xffc7c0a8);
    if (s == "Disco")    return mustard;
    if (s == "Funk")     return juce::Colour (0xffcf9a5a);
    if (s == "Reggae")   return olive;
    if (s == "Latin")    return juce::Colour (0xffe8a64a);
    return cream;
}
// Color de cada instrumento (pads y filas de la rejilla).
juce::Colour instColour (int i)
{
    static const juce::Colour c[] { brick, brick, brick, mustard, mustard, mustard, orange, orange, orange,
                                    olive, olive, olive, olive, olive };
    return i >= 0 && i < r78::kNumInst ? c[i] : cream;
}
}

// ------------------------------------------------------------------ ChoiceKeys
ChoiceKeys::ChoiceKeys (juce::Component& parent, juce::AudioProcessorValueTreeState& s, const juce::String& paramId,
                        std::function<juce::Colour (int)> colourFor)
{
    param = s.getParameter (paramId);
    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param);
    jassert (choice != nullptr);
    for (int i = 0; i < choice->choices.size(); ++i)
    {
        auto b = std::make_unique<juce::TextButton> (choice->choices[i]);
        b->getProperties().set ("led", true);
        b->getProperties().set ("keyColour", (juce::int64) colourFor (i).getARGB());
        b->onClick = [this, i, choice] {
            param->beginChangeGesture();
            param->setValueNotifyingHost (choice->convertTo0to1 ((float) i));
            param->endChangeGesture();
            timerCallback();
            if (onChange) onChange();
        };
        parent.addAndMakeVisible (*b);
        keys.push_back (std::move (b));
    }
    startTimerHz (10);
    timerCallback();
}

void ChoiceKeys::timerCallback()
{
    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param);
    const int idx = choice->getIndex();
    if (idx == shown) return;
    for (int i = 0; i < (int) keys.size(); ++i) keys[(size_t) i]->setToggleState (i == idx, juce::dontSendNotification);
    shown = idx;
    if (onChange) onChange();
}

// ------------------------------------------------------------------ MainPanel
void MainPanel::setupKnob (juce::Slider& kn, bool big)
{
    kn.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    kn.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    kn.setColour (juce::Slider::rotarySliderFillColourId, big ? orange : cream);
    kn.setPopupDisplayEnabled (true, true, this);
    kn.setMouseDragSensitivity (big ? 300 : 200);
    addAndMakeVisible (kn);
}

void MainPanel::refreshLengthBox()
{
    lengthBox.setSelectedId (proc.pattern.length.load() * 10 + proc.pattern.stepsPerBeat.load(), juce::dontSendNotification);
}

MainPanel::MainPanel (RitmoProcessor& p) : proc (p), grid (p), fxPanel (p)
{
    // ---- Ritmos de fábrica: una tecla con LED por ritmo, coloreada por estilo ----
    const auto& rhythms = r78::factoryRhythms();
    rhythmKeys = std::make_unique<ChoiceKeys> (*this, proc.apvts, "rhythm",
                                               [&rhythms] (int i) { return styleColour (rhythms[(size_t) i].style); });
    rhythmKeys->onChange = [this] {
        juce::Timer::callAfterDelay (120, [safe = juce::Component::SafePointer<MainPanel> (this)] {
            if (safe) { safe->refreshLengthBox(); safe->grid.repaint(); }
        });
    };

    // ---- Transporte ----
    playBtn.setClickingTogglesState (true);
    playBtn.getProperties().set ("keyColour", (juce::int64) brick.getARGB());
    playBtn.getProperties().set ("led", true);
    addAndMakeVisible (playBtn);
    playAtt = std::make_unique<ButtonAttachment> (proc.apvts, "play", playBtn);

    variationKeys = std::make_unique<ChoiceKeys> (*this, proc.apvts, "variation", [] (int) { return cream; });
    fillKeys = std::make_unique<ChoiceKeys> (*this, proc.apvts, "fill", [] (int) { return mustard; });

    struct L { int len, spb; const char* name; };
    for (auto l : { L { 8, 4, "8 pasos (2/4)" }, L { 12, 4, "12 pasos (3/4)" }, L { 16, 4, "16 pasos (4/4)" },
                    L { 24, 4, "24 pasos (6/4)" }, L { 32, 4, "32 pasos (2 x 4/4)" }, L { 9, 3, "9 pasos (3/4 tresillos)" },
                    L { 12, 3, "12 pasos (12/8)" }, L { 24, 3, "24 pasos (2 x 12/8)" } })
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

    setupKnob (tempoKnob, true); setupKnob (swingKnob); setupKnob (accentKnob); setupKnob (masterKnob, true);
    knobAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, "tempo", tempoKnob));
    knobAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, "swing", swingKnob));
    knobAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, "accent", accentKnob));
    knobAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, "master", masterKnob));
    tempoKnob.setTextValueSuffix (" BPM");
    swingKnob.setTextValueSuffix (" %");
    addAndMakeVisible (syncBtn);
    syncAtt = std::make_unique<ButtonAttachment> (proc.apvts, "sync", syncBtn);
    syncBtn.setTooltip (utf8 ("En el DAW, seguir su transporte y tempo"));

    // ---- Rejilla de patrón ----
    const char* tabNames[3] { "Variación A", "Variación B", "Fill" };
    for (int v = 0; v < 3; ++v)
    {
        auto& t = varTabs[(size_t) v];
        t.setButtonText (utf8 (tabNames[v]));
        t.setRadioGroupId (77);
        t.setClickingTogglesState (true);
        t.getProperties().set ("led", true);
        t.onClick = [this, v] { grid.editVar = v; grid.repaint(); };
        addAndMakeVisible (t);
    }
    varTabs[0].setToggleState (true, juce::dontSendNotification);
    clearBtn.getProperties().set ("keyColour", (juce::int64) juce::Colour (0xff8c8a82).getARGB());
    clearBtn.onClick = [this] {
        for (int r = 0; r < r78::kRows; ++r)
            for (int s = 0; s < r78::kMaxSteps; ++s) proc.pattern.set (grid.editVar, r, s, false);
        grid.repaint();
    };
    addAndMakeVisible (clearBtn);
    grid.rowColour = [] (int r) { return r == r78::kAccentRow ? ledOn : instColour (r); };
    addAndMakeVisible (grid);

    // ---- Tiras de instrumento ----
    for (int i = 0; i < r78::kNumInst; ++i)
    {
        auto& s = strips[(size_t) i];
        s.pad.setButtonText (utf8 (r78::kInstInfo[(size_t) i].name));
        s.pad.getProperties().set ("keyColour", (juce::int64) instColour (i).getARGB());
        s.pad.setTooltip (utf8 ("Clic para escuchar · Mayús+clic = acento"));
        s.pad.onClick = [this, i] { proc.auditionFromUi (i, juce::ModifierKeys::currentModifiers.isShiftDown()); };
        addAndMakeVisible (s.pad);
        static constexpr const char* ids[5] { "level", "tune", "decay", "tone", "pan" };
        for (int k = 0; k < 5; ++k)
        {
            setupKnob (s.knobs[(size_t) k]);
            s.sAtt.push_back (std::make_unique<SliderAttachment> (proc.apvts, RitmoProcessor::pid (i, ids[k]), s.knobs[(size_t) k]));
        }
        addAndMakeVisible (s.mute);
        s.mAtt = std::make_unique<ButtonAttachment> (proc.apvts, RitmoProcessor::pid (i, "mute"), s.mute);
        s.send.setColour (juce::ToggleButton::tickColourId, orange);
        s.send.setTooltip (utf8 ("Envía este instrumento a los efectos"));
        addAndMakeVisible (s.send);
        s.sendAtt = std::make_unique<ButtonAttachment> (proc.apvts, RitmoProcessor::pid (i, "send"), s.send);
    }

    addChildComponent (fxPanel);
    const char* pages[2] { "Instrumentos", "Efectos y máster" };
    for (int t = 0; t < 2; ++t)
    {
        auto& b = pageTabs[(size_t) t];
        b.setButtonText (utf8 (pages[t]));
        b.setRadioGroupId (88);
        b.setClickingTogglesState (true);
        b.getProperties().set ("led", true);
        b.onClick = [this, t] { showPage (t); };
        addAndMakeVisible (b);
    }
    showPage (0);
    setSize (kW, kH);
    startTimerHz (15);
}

MainPanel::~MainPanel() = default;

void MainPanel::showPage (int newPage)
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

void MainPanel::timerCallback()
{
    // Medidor de compresión del máster y LED de reproducción.
    const float gr = proc.compGainReduction.load();
    if (std::abs (gr - shownGr) > 0.1f) { shownGr = gr; repaint (kW - 260, 8, 60, 46); }
    const bool playing = proc.currentStep.load() >= 0;
    if (playing != lastPlaying) { lastPlaying = playing; repaint(); }
}

void MainPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    drawWood (g, { 0, 0, kCheek, kH }, 7);
    drawWood (g, { kW - kCheek, 0, kCheek, kH }, 11);

    // ---- Cabecera: logotipo propio ----
    auto head = juce::Rectangle<int> (kCheek, 0, kW - 2 * kCheek, kHeaderH);
    g.setColour (juce::Colour (0xff121110));
    g.fillRect (head);
    // Franjas de color de época
    const juce::Colour stripes[] { brick, orange, mustard };
    for (int i = 0; i < 3; ++i) { g.setColour (stripes[i]); g.fillRect (kCheek, kHeaderH - 9 + i * 3, kW - 2 * kCheek, 2); }
    g.setColour (cream);
    g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    g.drawText ("RITMO", kCheek + 18, 6, 140, 44, juce::Justification::centredLeft);
    g.setColour (orange);
    g.drawText ("CR-78", kCheek + 140, 6, 120, 44, juce::Justification::centredLeft);
    g.setColour (cream.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (utf8 ("CAJA DE RITMOS ANALÓGICA · 14 INSTRUMENTOS · 35 RITMOS"), kCheek + 270, 22, 420, 16,
                juce::Justification::centredLeft);

    // Medidor de compresión
    auto meter = juce::Rectangle<int> (kW - 250, 14, 40, 30);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (meter.toFloat(), 3.0f);
    const int segs = 8;
    for (int s = 0; s < segs; ++s)
    {
        const bool lit = shownGr > (float) s * 2.0f + 0.5f;
        g.setColour (lit ? (s < 5 ? mustard : ledOn) : juce::Colour (0xff2c2a26));
        g.fillRect (meter.getX() + 4 + s * 4, meter.getY() + 6, 3, 18);
    }
    g.setColour (cream.withAlpha (0.7f));
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("COMP", meter.getX(), meter.getBottom(), meter.getWidth(), 12, juce::Justification::centred);
    g.drawText ("MASTER", kW - kCheek - 120, 46, 70, 12, juce::Justification::centred);

    // ---- Rótulos de secciones ----
    drawSectionLabel (g, { kCheek + 14, kRowA - 2, 640, 14 }, utf8 ("RITMOS"));
    drawSectionLabel (g, { 720, kRowA - 2, kW - kCheek - 14 - 720, 14 }, utf8 ("TRANSPORTE"));
    drawSectionLabel (g, { kCheek + 14, kGridLabel, kW - 2 * kCheek - 28, 14 },
                      utf8 ("PATRÓN · clic o arrastrar para programar · fila roja = acento"));

    g.setColour (cream.withAlpha (0.75f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    auto under = [&] (juce::Component& c, const char* t) {
        g.drawText (utf8 (t), c.getX() - 10, c.getBottom() - 2, c.getWidth() + 20, 12, juce::Justification::centred);
    };
    under (tempoKnob, "TEMPO"); under (swingKnob, "SWING"); under (accentKnob, "ACENTO");
    g.drawText (utf8 ("VARIACIÓN"), variationKeys->keys.front()->getX(), variationKeys->keys.front()->getY() - 13, 160, 12,
                juce::Justification::centredLeft);
    g.drawText (utf8 ("FILL CADA … COMPASES"), fillKeys->keys.front()->getX(), fillKeys->keys.front()->getY() - 13, 200, 12,
                juce::Justification::centredLeft);
    g.drawText (utf8 ("LONGITUD"), lengthBox.getX(), lengthBox.getY() - 13, 120, 12, juce::Justification::centredLeft);

    // ---- Zona inferior ----
    g.setColour (panel2);
    g.fillRect (kCheek + 8, kPageTop - 4, kW - 2 * kCheek - 16, kH - kPageTop - 4);
    if (page == 0)
    {
        g.setColour (cream.withAlpha (0.6f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        for (auto& s : strips)
            for (int k = 0; k < 5; ++k)
            {
                auto& kn = s.knobs[(size_t) k];
                g.drawText (upper (utf8 (kKnobLabels[k])), kn.getX() - 8, kn.getBottom() - 3, kn.getWidth() + 16, 11,
                            juce::Justification::centred);
            }
    }
}

void MainPanel::resized()
{
    // Ritmos: 7 columnas x 5 filas
    const int keyW = 90, keyH = 34;
    for (int i = 0; i < (int) rhythmKeys->keys.size(); ++i)
        rhythmKeys->keys[(size_t) i]->setBounds (kCheek + 14 + (i % 7) * (keyW + 2), kRowA + 16 + (i / 7) * (keyH + 2), keyW, keyH);

    // Transporte
    playBtn.setBounds (720, kRowA + 18, 96, 66);
    tempoKnob.setBounds (830, kRowA + 12, 76, 76);
    swingKnob.setBounds (918, kRowA + 22, 56, 56);
    accentKnob.setBounds (984, kRowA + 22, 56, 56);
    syncBtn.setBounds (1056, kRowA + 22, 100, 22);
    for (int i = 0; i < (int) variationKeys->keys.size(); ++i)
        variationKeys->keys[(size_t) i]->setBounds (720 + i * 54, kRowA + 120, 52, 30);
    for (int i = 0; i < (int) fillKeys->keys.size(); ++i)
        fillKeys->keys[(size_t) i]->setBounds (900 + i * 50, kRowA + 120, 48, 30);
    lengthBox.setBounds (1056, kRowA + 66, 150, 24);
    masterKnob.setBounds (kW - kCheek - 110, 2, 50, 46);

    // Rejilla
    for (int v = 0; v < 3; ++v) varTabs[(size_t) v].setBounds (kCheek + 14 + StepGrid::labelW + v * 112, kGridTabs - 2, 108, 28);
    clearBtn.setBounds (kCheek + 14 + StepGrid::labelW + 3 * 112 + 12, kGridTabs + 6, 80, 20);
    grid.setBounds (kCheek + 14, kGridTop, kW - 2 * kCheek - 28, kGridH);

    // Páginas
    pageTabs[0].setBounds (kCheek + 14, kPageTabs - 4, 130, 30);
    pageTabs[1].setBounds (kCheek + 150, kPageTabs - 4, 160, 30);

    // Tiras: pad, NIVEL grande, Afinar/Decay, Tono/Pan, Mute/FX
    const int x0 = kCheek + 12 + ((kW - 2 * kCheek - 24) - kStripW * r78::kNumInst) / 2;
    for (int i = 0; i < r78::kNumInst; ++i)
    {
        auto& s = strips[(size_t) i];
        const int sx = x0 + i * kStripW, y = kPageTop + 4;
        s.pad.setBounds (sx + 3, y, kStripW - 6, 30);
        s.knobs[0].setBounds (sx + (kStripW - 44) / 2, y + 36, 44, 44);
        s.knobs[1].setBounds (sx + 4, y + 92, 36, 36);
        s.knobs[2].setBounds (sx + kStripW - 40, y + 92, 36, 36);
        s.knobs[3].setBounds (sx + 4, y + 140, 36, 36);
        s.knobs[4].setBounds (sx + kStripW - 40, y + 140, 36, 36);
        s.mute.setBounds (sx + 3, y + 190, 50, 20);
        s.send.setBounds (sx + 52, y + 190, 32, 20);
    }
    fxPanel.setBounds (kCheek + 12, kPageTop, kW - 2 * kCheek - 24, kH - kPageTop - 8);
}

// ------------------------------------------------------------------ Editor escalable
RitmoEditor::RitmoEditor (RitmoProcessor& p) : AudioProcessorEditor (&p), panel (p)
{
    setLookAndFeel (&laf);
    panel.setLookAndFeel (&laf);
    addAndMakeVisible (panel);
    setResizable (true, true);
    const double ratio = (double) MainPanel::kW / MainPanel::kH;
    getConstrainer()->setFixedAspectRatio (ratio);
    setResizeLimits (MainPanel::kW / 2, MainPanel::kH / 2, MainPanel::kW * 2, MainPanel::kH * 2);
    setSize (MainPanel::kW * 9 / 10, MainPanel::kH * 9 / 10);
}

RitmoEditor::~RitmoEditor()
{
    panel.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void RitmoEditor::resized()
{
    const float s = (float) getWidth() / (float) MainPanel::kW;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, MainPanel::kW, MainPanel::kH);
}
