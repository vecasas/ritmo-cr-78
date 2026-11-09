#pragma once
#include "PluginProcessor.h"
#include "ui/VintageLookAndFeel.h"

// Rejilla de pasos: una fila por instrumento más la fila de acentos.
// Clic para activar o desactivar un paso; arrastrar pinta varios.
class StepGrid : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid (RitmoProcessor& p) : proc (p) { startTimerHz (30); }

    int editVar = r78::VarA;
    std::function<juce::Colour (int)> rowColour;

    void paint (juce::Graphics& g) override
    {
        const int len = proc.pattern.length.load();
        const int spb = proc.pattern.stepsPerBeat.load();
        const int playing = proc.currentVar.load() == editVar ? proc.currentStep.load() : -1;
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));

        // LED de posición encima de cada columna: lo dibuja la fila 0 con un borde.
        for (int r = 0; r < r78::kRows; ++r)
        {
            const bool accentRow = r == r78::kAccentRow;
            g.setColour (juce::Colour (0xffeadfca).withAlpha (0.8f));
            const juce::String name = accentRow ? juce::String ("Acento")
                                                : juce::String (juce::CharPointer_UTF8 (r78::kInstInfo[(size_t) r].name));
            g.drawText (vintage::upper (name), 0, r * cellH, labelW - 8, cellH, juce::Justification::centredRight);
            const juce::Colour onC = rowColour ? rowColour (r) : (accentRow ? juce::Colour (0xffe0b040) : juce::Colour (0xffd9822b));

            for (int s = 0; s < len; ++s)
            {
                auto cell = cellBounds (r, s);
                const bool on = proc.pattern.get (editVar, r, s);
                const bool beatStart = (s % spb) == 0;
                const juce::Colour off = beatStart ? juce::Colour (0xff3a3731) : juce::Colour (0xff2b2925);
                auto cf = cell.toFloat();
                if (on)
                {
                    juce::ColourGradient gr (onC.brighter (0.2f), cf.getX(), cf.getY(), onC.darker (0.3f), cf.getX(), cf.getBottom(), false);
                    g.setGradientFill (gr);
                }
                else
                    g.setColour (off);
                g.fillRoundedRectangle (cf, 2.5f);
                if (s == playing)
                {
                    g.setColour (juce::Colours::white.withAlpha (on ? 0.45f : 0.12f));
                    g.fillRoundedRectangle (cell.toFloat(), 2.5f);
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int r, s;
        if (! hit (e.getPosition(), r, s)) return;
        paintValue = ! proc.pattern.get (editVar, r, s);
        proc.pattern.set (editVar, r, s, paintValue);
        lastR = r; lastS = s;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        int r, s;
        if (! hit (e.getPosition(), r, s) || (r == lastR && s == lastS)) return;
        proc.pattern.set (editVar, r, s, paintValue);
        lastR = r; lastS = s;
        repaint();
    }

    static constexpr int labelW = 96, cellH = 20;

private:
    juce::Rectangle<int> cellBounds (int r, int s) const
    {
        const int len = proc.pattern.length.load();
        const float w = (float) (getWidth() - labelW) / (float) juce::jmax (1, len);
        return juce::Rectangle<int> (labelW + (int) (s * w) + 1, r * cellH + 1, (int) w - 2, cellH - 2);
    }
    bool hit (juce::Point<int> p, int& r, int& s) const
    {
        const int len = proc.pattern.length.load();
        if (p.x < labelW) return false;
        r = p.y / cellH;
        s = (int) ((p.x - labelW) / ((float) (getWidth() - labelW) / (float) juce::jmax (1, len)));
        return r >= 0 && r < r78::kRows && s >= 0 && s < len;
    }
    void timerCallback() override { repaint(); }

    RitmoProcessor& proc;
    bool paintValue = true;
    int lastR = -1, lastS = -1;
};
