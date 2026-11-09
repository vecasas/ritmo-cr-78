#pragma once
#include "PluginProcessor.h"
#include "ui/VintageLookAndFeel.h"

// Lista de pasos del modo canción (hasta 99). Cada casilla es un patrón de usuario,
// FIN (para) o BUCLE (vuelve al principio).
//  - Clic: selecciona el paso (la rejilla pasa a editar ese patrón).
//  - Arrastrar arriba/abajo o rueda: cambia el número de patrón.
//  - Clic en la primera casilla vacía: añade un paso.
//  - Clic derecho: elegir patrón, Fin, Bucle, Insertar, Eliminar.
class SongStrip : public juce::Component, private juce::Timer
{
public:
    explicit SongStrip (RitmoProcessor& p) : proc (p) { startTimerHz (15); }

    static constexpr int kCols = 20, kRowsN = 5;

    void paint (juce::Graphics& g) override
    {
        const int n = proc.songLength.load();
        const int sel = proc.songEditPos.load();
        const int playing = proc.songPlayPos.load();
        for (int i = 0; i < RitmoProcessor::kSongSteps; ++i)
        {
            auto b = box (i).toFloat();
            const bool used = i < n;
            juce::Colour fill = used ? juce::Colour (0xff3a3731) : juce::Colour (0xff24221f);
            int e = used ? proc.song[(size_t) i].load() : 0;
            if (used && e == RitmoProcessor::kSongEnd) fill = vintage::brick.darker (0.3f);
            if (used && e == RitmoProcessor::kSongLoop) fill = vintage::olive.darker (0.2f);
            g.setColour (fill);
            g.fillRoundedRectangle (b, 3.0f);
            if (i == sel)
            {
                g.setColour (vintage::orange);
                g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 2.0f);
            }
            if (i == playing)
            {
                g.setColour (vintage::ledOn);
                g.fillEllipse (b.getRight() - 7, b.getY() + 3, 4, 4);
            }
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.setColour (vintage::cream.withAlpha (0.35f));
            g.drawText (juce::String (i + 1), b.reduced (3, 1).toNearestInt(), juce::Justification::topLeft);
            if (! used)
            {
                if (i == n)
                {
                    g.setColour (vintage::cream.withAlpha (0.5f));
                    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
                    g.drawText ("+", b.toNearestInt(), juce::Justification::centred);
                }
                continue;
            }
            g.setColour (vintage::cream);
            g.setFont (juce::FontOptions (e == RitmoProcessor::kSongLoop ? 8.5f : 11.0f, juce::Font::bold));
            const juce::String t = e == RitmoProcessor::kSongEnd ? juce::String ("FIN")
                                 : e == RitmoProcessor::kSongLoop ? juce::String (juce::CharPointer_UTF8 ("BUCLE"))
                                 : juce::String ("P") + juce::String (e + 1);
            g.drawText (t, b.withTrimmedTop (8).toNearestInt(), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.getPosition());
        if (i < 0) return;
        const int n = proc.songLength.load();
        if (i > n) return;
        if (i == n)
        {
            if (n >= RitmoProcessor::kSongSteps) return;
            // Añadir: repite el último patrón (o el 1 si la canción está vacía).
            int last = n > 0 ? proc.song[(size_t) (n - 1)].load() : 0;
            if (last < 0) last = 0;
            proc.song[(size_t) n].store (last);
            proc.songLength.store (n + 1);
        }
        proc.songEditPos.store (i);
        if (e.mods.isPopupMenu()) showMenu (i);
        dragStartValue = proc.song[(size_t) i].load();
        repaint();
        if (onSelect) onSelect();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || dragStartValue < 0) return;
        const int i = proc.songEditPos.load();
        if (i >= proc.songLength.load()) return;
        setEntry (i, dragStartValue - e.getDistanceFromDragStartY() / 6);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int i = indexAt (e.getPosition());
        if (i < 0 || i >= proc.songLength.load()) return;
        const int cur = proc.song[(size_t) i].load();
        if (cur < 0) return;
        proc.songEditPos.store (i);
        setEntry (i, cur + (w.deltaY > 0 ? 1 : -1));
    }

    std::function<void()> onSelect;   // la selección o el patrón del paso cambió
    // Pone el patrón (0..98) del paso seleccionado; lo usan las teclas de patrón.
    void setSelectedPattern (int idx)
    {
        const int i = proc.songEditPos.load();
        if (i < proc.songLength.load()) setEntry (i, idx);
    }

private:
    juce::Rectangle<int> box (int i) const
    {
        const float w = (float) getWidth() / kCols, h = (float) getHeight() / kRowsN;
        return juce::Rectangle<float> ((i % kCols) * w + 1, (i / kCols) * h + 1, w - 2, h - 2).toNearestInt();
    }
    int indexAt (juce::Point<int> p) const
    {
        const int c = (int) (p.x * kCols / juce::jmax (1, getWidth())), r = (int) (p.y * kRowsN / juce::jmax (1, getHeight()));
        const int i = r * kCols + c;
        return c >= 0 && c < kCols && r >= 0 && r < kRowsN && i < RitmoProcessor::kSongSteps ? i : -1;
    }
    void setEntry (int i, int idx)
    {
        proc.song[(size_t) i].store (juce::jlimit (0, RitmoProcessor::kUserSlots - 1, idx));
        repaint();
        if (onSelect) onSelect();
    }
    void showMenu (int i)
    {
        juce::PopupMenu m, pats;
        for (int p = 0; p < RitmoProcessor::kUserSlots; ++p)
            pats.addItem (100 + p, "P" + juce::String (p + 1) + (proc.patternHasNotes (proc.userPatterns[(size_t) p]) ? " *" : ""));
        m.addSubMenu (juce::String (juce::CharPointer_UTF8 ("Patrón")), pats);
        m.addItem (1, "Fin (parar)");
        m.addItem (2, juce::String (juce::CharPointer_UTF8 ("Bucle (volver al inicio)")));
        m.addSeparator();
        m.addItem (3, juce::String (juce::CharPointer_UTF8 ("Insertar paso aquí")), proc.songLength.load() < RitmoProcessor::kSongSteps);
        m.addItem (4, "Eliminar paso");
        m.addItem (5, juce::String (juce::CharPointer_UTF8 ("Vaciar canción")));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, i] (int r)
        {
            auto& s = proc.song;
            const int n = proc.songLength.load();
            if (r >= 100) setEntry (i, r - 100);
            else if (r == 1) s[(size_t) i].store (RitmoProcessor::kSongEnd);
            else if (r == 2) s[(size_t) i].store (RitmoProcessor::kSongLoop);
            else if (r == 3 && n < RitmoProcessor::kSongSteps)
            {
                for (int k = n; k > i; --k) s[(size_t) k].store (s[(size_t) (k - 1)].load());
                proc.songLength.store (n + 1);
            }
            else if (r == 4 && n > 0)
            {
                for (int k = i; k < n - 1; ++k) s[(size_t) k].store (s[(size_t) (k + 1)].load());
                proc.songLength.store (n - 1);
                proc.songEditPos.store (juce::jlimit (0, juce::jmax (0, n - 2), i));
            }
            else if (r == 5) { proc.songLength.store (0); proc.songEditPos.store (0); }
            repaint();
            if (onSelect) onSelect();
        });
    }
    void timerCallback() override
    {
        const int p = proc.songPlayPos.load();
        if (p != shownPlay) { shownPlay = p; repaint(); }
    }

    RitmoProcessor& proc;
    int dragStartValue = -1, shownPlay = -2;
};
