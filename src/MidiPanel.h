#pragma once
#include "PluginProcessor.h"
#include "ui/VintageLookAndFeel.h"

// Tecla que, al arrastrarla, suelta un archivo .mid en el DAW o en el escritorio.
class MidiDragButton : public juce::TextButton
{
public:
    std::function<juce::File()> makeFile;
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! makeFile) return;
        dragging = true;
        const auto f = makeFile();
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this,
                                                                         [this] { dragging = false; });
        else
            dragging = false;
    }
    void mouseUp (const juce::MouseEvent& e) override { juce::TextButton::mouseUp (e); }
private:
    bool dragging = false;
};

// Página MIDI: canal de entrada, velocidad y acento, salida del ritmo por MIDI
// y el mapa de notas de los 14 instrumentos con "Aprender".
class MidiPanel : public juce::Component, private juce::Timer
{
public:
    explicit MidiPanel (RitmoProcessor& p) : proc (p)
    {
        auto u = [] (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); };

        // ---- Entrada ----
        addCombo (inChBox, "midi_in_ch", 0, 16, [] (int v) { return v == 0 ? juce::String ("Omni") : juce::String (v); });
        addCombo (accNoteBox, "midi_acc_note", -1, 127, [] (int v) {
            return v < 0 ? juce::String ("Ninguna") : juce::MidiMessage::getMidiNoteName (v, true, true, 3) + "  (" + juce::String (v) + ")"; });
        if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("midi_vel")))
            velBox.addItemList (pc->choices, 1);
        addAndMakeVisible (velBox);
        velAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "midi_vel", velBox);
        velBox.setTooltip (u ("Dinámica: el volumen sigue a la velocidad y por encima del umbral hay acento. "
                              "Dos niveles: normal o acento, como el secuenciador. Fija: siempre igual; el acento solo con la nota de acento."));
        accNoteBox.setTooltip (u ("Mientras esta nota está pulsada (o si llega a la vez), los golpes suenan con acento. No dispara ningún instrumento."));

        thrKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        thrKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        thrKnob.setPopupDisplayEnabled (true, true, this);
        thrKnob.setColour (juce::Slider::rotarySliderFillColourId, vintage::cream);
        addAndMakeVisible (thrKnob);
        thrAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "midi_acc_thr", thrKnob);
        thrKnob.setTooltip (u ("Velocidad a partir de la cual un golpe MIDI lleva acento"));

        // ---- Salida ----
        addAndMakeVisible (outBtn);
        outBtn.setTooltip (u ("Envía cada golpe del ritmo (y de los pads) como nota MIDI, con las notas del mapa"));
        outAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "midi_out", outBtn);
        addCombo (outChBox, "midi_out_ch", 1, 16, [] (int v) { return juce::String (v); });

        // ---- Exportar arrastrando ----
        for (int b : { 1, 2, 4, 8, 16 }) barsBox.addItem (juce::String (b) + (b == 1 ? u (" compás") : u (" compases")), b);
        barsBox.setSelectedId (4, juce::dontSendNotification);
        barsBox.setTooltip (u ("Cuántos compases exporta (con variación y fills). En modo canción exporta la canción entera."));
        addAndMakeVisible (barsBox);
        dragBtn.setButtonText (u ("Arrastrar MIDI"));
        dragBtn.getProperties().set ("keyColour", (juce::int64) vintage::mustard.getARGB());
        dragBtn.setTooltip (u ("Arrastra esta tecla a una pista del DAW o al escritorio para soltar el ritmo como archivo MIDI"));
        dragBtn.makeFile = [this] { return proc.exportMidiFile (juce::jmax (1, barsBox.getSelectedId())); };
        addAndMakeVisible (dragBtn);

        // ---- Mapa de notas ----
        for (int i = 0; i < r78::kNumInst; ++i)
        {
            auto& b = learnBtns[(size_t) i];
            b.setButtonText ("Aprender");
            auto& oc = outBoxes[(size_t) i];
            if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (RitmoProcessor::pid (i, "out"))))
                oc.addItemList (pc->choices, 1);
            oc.setTooltip (u ("Salida de audio del instrumento. Las salidas 2 a 8 van en seco (sin efectos) y hay que activarlas en el DAW."));
            addAndMakeVisible (oc);
            outAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, RitmoProcessor::pid (i, "out"), oc));
            b.getProperties().set ("led", true);
            b.getProperties().set ("keyColour", (juce::int64) juce::Colour (0xff8c8a82).getARGB());
            b.setTooltip (u ("Pulsa y toca una nota en tu teclado o pad MIDI para asignarla a este instrumento"));
            b.onClick = [this, i] { proc.learnInst.store (proc.learnInst.load() == i ? -1 : i); timerCallback(); };
            addAndMakeVisible (b);
        }
        resetBtn.getProperties().set ("keyColour", (juce::int64) juce::Colour (0xff8c8a82).getARGB());
        resetBtn.setTooltip (u ("Vuelve a las notas por defecto (General MIDI)"));
        resetBtn.onClick = [this] { proc.midiMap.reset(); proc.learnInst.store (-1); repaint(); };
        addAndMakeVisible (resetBtn);
        timerCallback();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour ink = vintage::cream;
        auto box = [&] (juce::Rectangle<int> r, const char* title) {
            g.setColour (juce::Colour (0xff1b1a18));
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (ink.withAlpha (0.18f));
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (vintage::orange);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (juce::CharPointer_UTF8 (title)), r.getX() + 10, r.getY() + 6, r.getWidth() - 20, 14,
                        juce::Justification::centredLeft);
        };
        box (inArea, "ENTRADA MIDI");
        box (outArea, "SALIDA MIDI");
        box (mapArea, "MAPA DE NOTAS Y SALIDAS DE AUDIO");

        g.setColour (ink.withAlpha (0.6f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        auto above = [&] (juce::Component& c, const char* t) {
            g.drawText (juce::String (juce::CharPointer_UTF8 (t)), c.getX(), c.getY() - 13, c.getWidth() + 40, 12, juce::Justification::centredLeft);
        };
        above (inChBox, "CANAL"); above (velBox, "VELOCIDAD"); above (accNoteBox, "NOTA DE ACENTO"); above (outChBox, "CANAL");
        above (barsBox, "EXPORTAR");
        g.drawText ("UMBRAL", thrKnob.getX() - 10, thrKnob.getBottom(), thrKnob.getWidth() + 20, 12, juce::Justification::centred);

        // Última nota recibida
        const int last = proc.lastMidiNote.load();
        g.setColour (ink.withAlpha (0.5f));
        g.drawText (last < 0 ? juce::String (juce::CharPointer_UTF8 ("Última nota: —"))
                             : juce::String (juce::CharPointer_UTF8 ("Última nota: ")) + juce::MidiMessage::getMidiNoteName (last, true, true, 3)
                                   + " (" + juce::String (last) + ")",
                    inArea.getX() + 10, inArea.getBottom() - 20, inArea.getWidth() - 20, 14, juce::Justification::centredLeft);

        // Celdas del mapa: nombre y nota de cada instrumento.
        for (int i = 0; i < r78::kNumInst; ++i)
        {
            auto c = cell (i);
            const int n = proc.midiMap.note[(size_t) i].load();
            g.setColour (juce::Colour (0xff2b2925));
            g.fillRoundedRectangle (c.toFloat(), 3.0f);
            g.setColour (ink.withAlpha (0.85f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (vintage::upper (juce::String (juce::CharPointer_UTF8 (r78::kInstInfo[(size_t) i].name))),
                        c.getX(), c.getY() + 3, c.getWidth(), 13, juce::Justification::centred);
            g.setColour (vintage::mustard);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (n < 0 ? juce::String ("--") : juce::MidiMessage::getMidiNoteName (n, true, true, 3) + " · " + juce::String (n),
                        c.getX(), c.getY() + 17, c.getWidth(), 16, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        inArea = r.removeFromLeft (300);
        r.removeFromLeft (8);
        outArea = r.removeFromLeft (180);
        r.removeFromLeft (8);
        mapArea = r;

        inChBox.setBounds (inArea.getX() + 12, inArea.getY() + 44, 80, 22);
        velBox.setBounds (inArea.getX() + 104, inArea.getY() + 44, 120, 22);
        thrKnob.setBounds (inArea.getX() + 238, inArea.getY() + 34, 44, 44);
        accNoteBox.setBounds (inArea.getX() + 12, inArea.getY() + 106, 212, 22);

        outBtn.setBounds (outArea.getX() + 12, outArea.getY() + 34, 160, 22);
        outChBox.setBounds (outArea.getX() + 12, outArea.getY() + 80, 80, 22);
        barsBox.setBounds (outArea.getX() + 12, outArea.getY() + 128, 156, 22);
        dragBtn.setBounds (outArea.getX() + 12, outArea.getY() + 158, 156, 34);

        resetBtn.setBounds (mapArea.getRight() - 150, mapArea.getY() + 4, 140, 20);
        for (int i = 0; i < r78::kNumInst; ++i)
        {
            auto c = cell (i);
            learnBtns[(size_t) i].setBounds (c.getX() + 6, c.getY() + 34, c.getWidth() - 12, 30);
            outBoxes[(size_t) i].setBounds (c.getX() + 6, c.getY() + 66, c.getWidth() - 12, 20);
        }
    }

private:
    juce::Rectangle<int> cell (int i) const
    {
        const int cols = 7, x0 = mapArea.getX() + 8, y0 = mapArea.getY() + 28;
        const int w = (mapArea.getWidth() - 16) / cols, h = (mapArea.getHeight() - 34) / 2;
        return { x0 + (i % cols) * w + 2, y0 + (i / cols) * h + 2, w - 4, h - 4 };
    }

    // ComboBox enlazado a un parámetro entero (los ids son valor - mínimo + 1).
    void addCombo (juce::ComboBox& box, const juce::String& id, int lo, int hi, std::function<juce::String (int)> name)
    {
        for (int v = lo; v <= hi; ++v) box.addItem (name (v), v - lo + 1);
        auto* prm = proc.apvts.getParameter (id);
        box.onChange = [&box, prm, lo] {
            const int id2 = box.getSelectedId();
            if (id2 <= 0 || prm == nullptr) return;
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (id2 - 1 + lo)));
            prm->endChangeGesture();
        };
        combos.push_back ({ &box, prm, lo });
        addAndMakeVisible (box);
    }

    void timerCallback() override
    {
        for (auto& c : combos)
        {
            const int v = (int) std::lround (c.prm->convertFrom0to1 (c.prm->getValue()));
            if (c.box->getSelectedId() != v - c.lo + 1) c.box->setSelectedId (v - c.lo + 1, juce::dontSendNotification);
        }
        const int li = proc.learnInst.load();
        bool changed = li != shownLearn;
        for (int i = 0; i < r78::kNumInst; ++i)
        {
            learnBtns[(size_t) i].setToggleState (i == li, juce::dontSendNotification);
            const int n = proc.midiMap.note[(size_t) i].load();
            if (n != shownNotes[(size_t) i]) { shownNotes[(size_t) i] = n; changed = true; }
        }
        if (proc.lastMidiNote.load() != shownLast) { shownLast = proc.lastMidiNote.load(); changed = true; }
        shownLearn = li;
        if (changed) repaint();
    }

    struct ComboLink { juce::ComboBox* box; juce::RangedAudioParameter* prm; int lo; };

    RitmoProcessor& proc;
    juce::ComboBox inChBox, velBox, accNoteBox, outChBox;
    juce::Slider thrKnob;
    juce::ToggleButton outBtn { "Enviar el ritmo" };
    std::array<juce::TextButton, r78::kNumInst> learnBtns;
    juce::TextButton resetBtn { "Mapa por defecto" };
    std::array<juce::ComboBox, r78::kNumInst> outBoxes;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> outAtts;
    juce::ComboBox barsBox;
    MidiDragButton dragBtn;
    std::vector<ComboLink> combos;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> velAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thrAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> outAtt;
    juce::Rectangle<int> inArea, outArea, mapArea;
    std::array<int, r78::kNumInst> shownNotes {};
    int shownLearn = -2, shownLast = -2;
};
