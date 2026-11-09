#pragma once
#include "PluginProcessor.h"

// Panel de efectos y máster (interfaz provisional). Cada bloque tiene un botón
// de encendido y sus mandos; el compresor muestra la reducción de ganancia.
class FxPanel : public juce::Component, private juce::Timer
{
public:
    explicit FxPanel (RitmoProcessor& p) : proc (p)
    {
        addBlock ("OVERDRIVE", "od_on", { { "od_drive", "Drive" }, { "od_tone", "Tono" }, { "od_mix", "Mezcla" } });
        addBlock ("FLANGER", "fl_on", { { "fl_rate", "Velocidad" }, { "fl_depth", "Profund." }, { "fl_fb", "Realim." }, { "fl_mix", "Mezcla" } });
        addBlock ("DELAY", "dl_on", { { "dl_fb", "Realim." }, { "dl_damp", "Amortig." }, { "dl_mix", "Mezcla" } },
                  { { "dl_time", "Tiempo" } }, { { "dl_pp", "Ping-pong" } });
        addBlock ("REVERB GATE", "rv_on", { { "rv_size", "Tamaño" }, { "rv_gate", "Gate ms" }, { "rv_mix", "Mezcla" } },
                  {}, { { "rv_gate_on", "Gate" } });
        addBlock ("COMPRESOR", "comp_on", { { "comp_thr", "Umbral" }, { "comp_ratio", "Ratio" }, { "comp_att", "Ataque" },
                                            { "comp_rel", "Relaj." }, { "comp_makeup", "Ganancia" } });
        addBlock ("EQ MÁSTER", "eq_on", { { "eq_0", "60" }, { "eq_1", "150" }, { "eq_2", "400" }, { "eq_3", "1k" },
                                          { "eq_4", "3k" }, { "eq_5", "8k" } }, {}, {}, true);
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour ink (0xffece2c6);
        for (auto& b : blocks)
        {
            g.setColour (juce::Colour (0xff1b1a18));
            g.fillRoundedRectangle (b->area.toFloat(), 4.0f);
            g.setColour (ink.withAlpha (0.18f));
            g.drawRoundedRectangle (b->area.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (ink.withAlpha (0.55f));
            g.setFont (juce::FontOptions (11.0f));
            for (auto& c : b->controls)
                g.drawText (c.label, c.comp->getX() - 8, c.comp->getBottom(), c.comp->getWidth() + 16, 14, juce::Justification::centred);
        }
        // Medidor de reducción de ganancia del compresor.
        if (compBlock)
        {
            auto m = compBlock->area.withTrimmedLeft (compBlock->area.getWidth() - 18).reduced (5, 34);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRect (m);
            const float gr = juce::jlimit (0.0f, 20.0f, proc.compGainReduction.load());
            g.setColour (juce::Colour (0xffff3b2f));
            g.fillRect (m.withHeight ((int) (m.getHeight() * gr / 20.0f)));
        }
    }

    void resized() override
    {
        // Dos filas: efectos arriba, máster abajo.
        const int rowH = getHeight() / 2 - 3;
        int x = 0, y = 0;
        for (auto& b : blocks)
        {
            const int n = (int) b->controls.size();
            int w = 16 + (b.get() == compBlock ? 18 : 0);
            for (auto& c : b->controls)
                w += dynamic_cast<juce::ComboBox*> (c.comp.get()) ? 96 : (dynamic_cast<juce::ToggleButton*> (c.comp.get()) ? 76 : 58);
            w = juce::jmax (150, w);
            juce::ignoreUnused (n);
            if (b.get() == compBlock) { x = 0; y = rowH + 6; }
            b->area = { x, y, w, rowH };
            b->power.setBounds (b->area.getX() + 8, y + 4, w - 16, 32);
            int cx = b->area.getX() + 8;
            for (auto& c : b->controls)
            {
                if (dynamic_cast<juce::ComboBox*> (c.comp.get()))
                    c.comp->setBounds (cx, y + 52, 92, 24);
                else if (dynamic_cast<juce::ToggleButton*> (c.comp.get()))
                    c.comp->setBounds (cx, y + 52, 76, 24);
                else if (b->vertical)
                    c.comp->setBounds (cx + 12, y + 40, 30, rowH - 56);
                else
                    c.comp->setBounds (cx + 5, y + 42, 46, 46);
                cx += dynamic_cast<juce::ComboBox*> (c.comp.get()) ? 96 : (dynamic_cast<juce::ToggleButton*> (c.comp.get()) ? 76 : 58);
            }
            x += w + 8;
        }
    }

    int preferredWidth() const
    {
        int w = 0;
        for (auto& b : blocks) w += juce::jmax (150, (int) b->controls.size() * 58 + 16 + (b.get() == compBlock ? 18 : 0)) + 8;
        return w;
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    struct Ctl { std::unique_ptr<juce::Component> comp; juce::String label; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> s;
                 std::unique_ptr<ComboAttachment> c; std::unique_ptr<ButtonAttachment> b; };
    struct Block
    {
        juce::TextButton power;
        std::unique_ptr<ButtonAttachment> powerAtt;
        std::vector<Ctl> controls;
        juce::Rectangle<int> area;
        bool vertical = false;
    };
    struct Spec { const char* id; const char* label; };

    void addBlock (const char* title, const char* onId, std::initializer_list<Spec> knobs,
                   std::initializer_list<Spec> combos = {}, std::initializer_list<Spec> toggles = {}, bool vertical = false)
    {
        auto b = std::make_unique<Block>();
        b->vertical = vertical;
        b->power.setButtonText (juce::String (juce::CharPointer_UTF8 (title)));
        b->power.setClickingTogglesState (true);
        b->power.getProperties().set ("led", true);   // LED rojo = efecto encendido
        b->power.getProperties().set ("keyColour", (juce::int64) juce::Colour (0xffe0782a).getARGB());
        addAndMakeVisible (b->power);
        b->powerAtt = std::make_unique<ButtonAttachment> (proc.apvts, onId, b->power);
        for (auto& s : combos)
        {
            Ctl c; auto box = std::make_unique<juce::ComboBox>();
            if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (s.id))) box->addItemList (pc->choices, 1);
            c.c = std::make_unique<ComboAttachment> (proc.apvts, s.id, *box);
            c.label = juce::String (juce::CharPointer_UTF8 (s.label)); c.comp = std::move (box);
            addAndMakeVisible (*c.comp); b->controls.push_back (std::move (c));
        }
        for (auto& s : knobs)
        {
            Ctl c; auto k = std::make_unique<juce::Slider>();
            k->setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::RotaryVerticalDrag);
            k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k->setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffece2c6));
            k->setPopupDisplayEnabled (true, true, this);
            c.s = std::make_unique<SliderAttachment> (proc.apvts, s.id, *k);
            c.label = juce::String (juce::CharPointer_UTF8 (s.label)); c.comp = std::move (k);
            addAndMakeVisible (*c.comp); b->controls.push_back (std::move (c));
        }
        for (auto& s : toggles)
        {
            Ctl c; auto t = std::make_unique<juce::ToggleButton> (juce::String (juce::CharPointer_UTF8 (s.label)));
            t->setColour (juce::ToggleButton::textColourId, juce::Colour (0xffeadfca));
            c.b = std::make_unique<ButtonAttachment> (proc.apvts, s.id, *t);
            c.comp = std::move (t);
            addAndMakeVisible (*c.comp); b->controls.push_back (std::move (c));
        }
        if (juce::String (onId) == "comp_on") compBlock = b.get();
        blocks.push_back (std::move (b));
    }

    void timerCallback() override { if (compBlock) repaint (compBlock->area); }

    RitmoProcessor& proc;
    std::vector<std::unique_ptr<Block>> blocks;
    Block* compBlock = nullptr;
};
