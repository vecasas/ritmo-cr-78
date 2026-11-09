// Pruebas del secuenciador sin JUCE. Sale con código 1 si algo falla.
#include "../src/seq/Sequencer.h"
#include "../src/seq/MidiMap.h"
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main()
{
    using namespace r78;

    // Todos los ritmos de fábrica se cargan, tienen golpes y no exceden su longitud.
    const auto& list = factoryRhythms();
    CHECK (list.size() >= 34);
    for (auto& d : list)
    {
        Pattern p;
        loadRhythm (p, d);
        CHECK (p.length.load() == d.length);
        for (int v = 0; v < kNumVars; ++v)
        {
            int hits = 0;
            for (int r = 0; r < kNumInst; ++r)
                for (int s = 0; s < kMaxSteps; ++s)
                    if (p.get (v, r, s))
                    {
                        ++hits;
                        if (s >= d.length) std::printf ("  %s var %d fila %d: paso %d fuera de longitud\n", d.name, v, r, s);
                        CHECK (s < d.length);
                    }
            if (hits == 0) std::printf ("  %s var %d sin golpes\n", d.name, v);
            CHECK (hits > 0);
        }
    }

    // Reloj recto: 4 pasos por negra, a ppq 1.0 suena el paso 4.
    {
        StepClock c; c.locate (0.0, 4);
        int fired = 0; long last = -1;
        for (double ppq = 0; ppq < 1.0 + 1e-6; ppq += 1.0 / 4800.0)
            for (long g; (g = c.poll (ppq, 0.5f)) >= 0;) { ++fired; last = g; }
        CHECK (fired == 5);
        CHECK (last == 4);
    }

    // Swing 66%: el paso 1 suena en 2/3 del par (ppq = 0.5 * 0.66 / 1 ...).
    {
        StepClock c; c.locate (0.0, 4);
        CHECK (std::abs (c.stepTime (1, 0.66f) - (0.25 + 0.16 * 0.5)) < 1e-6);
        CHECK (std::abs (c.stepTime (2, 0.66f) - 0.5) < 1e-9);
    }

    // Recolocar a mitad de compás empieza en el siguiente paso.
    {
        StepClock c; c.locate (2.1, 4);
        CHECK (c.peekNext() == 9);
    }

    // Variaciones y fills.
    {
        PlayOptions o; o.variation = 2; o.fillEvery = 4;
        CHECK (variationForBar (0, o) == VarA);
        CHECK (variationForBar (1, o) == VarB);
        CHECK (variationForBar (3, o) == VarFill);
        CHECK (variationForBar (7, o) == VarFill);
        o.fillEvery = 0; o.variation = 1;
        CHECK (variationForBar (3, o) == VarB);
    }

    // Redoble de bongó cada 2 compases: semicorcheas en los pasos 8-15 del compás 2.
    {
        Pattern p; p.clear(); p.length.store (16); p.stepsPerBeat.store (4);
        p.set (VarA, (int) Inst::Kick, 0, true);
        PlayOptions o; o.fillEvery = 2; o.fillType = FillRollBongo;
        bool ev[kRows];
        int rollHits = 0;
        for (long g = 0; g < 32; ++g)
        {
            stepEvents (p, g, o, ev);
            if (ev[(int) Inst::HiBongo]) { ++rollHits; CHECK (g >= 24); }
            if (g % 16 == 0) CHECK (ev[(int) Inst::Kick]);   // el ritmo sigue sonando
        }
        CHECK (rollHits == 8);
        // Break: solo el primer paso del compás de fill.
        o.fillType = FillBreak;
        p.set (VarA, (int) Inst::Kick, 4, true);
        stepEvents (p, 16 + 4, o, ev);
        CHECK (! ev[(int) Inst::Kick]);
        stepEvents (p, 16, o, ev);
        CHECK (ev[(int) Inst::Kick]);
    }

    // Modo canción: patrón 0 (16 pasos), patrón 1 (8 pasos), bucle.
    {
        int entries[3] { 0, 1, kSongLoop };
        auto lenOf = [] (int idx) { return idx == 0 ? 16 : 8; };
        auto at = [&entries] (int i) { return entries[i]; };
        SongCursor c; long local = -1;
        CHECK (songStep (c, 0, 3, at, lenOf, local) == 0 && local == 0);
        CHECK (songStep (c, 15, 3, at, lenOf, local) == 0 && local == 15);
        CHECK (songStep (c, 16, 3, at, lenOf, local) == 1 && local == 0);
        CHECK (songStep (c, 23, 3, at, lenOf, local) == 1 && local == 7);
        CHECK (songStep (c, 24, 3, at, lenOf, local) == 0 && local == 0);   // bucle
        CHECK (c.pos == 0);
        // Con fin: tras el patrón 1 se para.
        entries[2] = kSongEnd;
        SongCursor c2;
        for (long g = 0; g < 24; ++g) CHECK (songStep (c2, g, 3, at, lenOf, local) >= 0);
        CHECK (songStep (c2, 24, 3, at, lenOf, local) == kSongEnd);
        // Lista vacía y lista sin fin (vuelve a empezar).
        SongCursor c3;
        CHECK (songStep (c3, 0, 0, at, lenOf, local) == kSongNone);
        CHECK (songStep (c3, 0, 1, at, lenOf, local) == 0);
        CHECK (songStep (c3, 16, 1, at, lenOf, local) == 0 && local == 0);
        // Solo un bucle: no se queda colgado.
        int loopOnly[1] { kSongLoop };
        SongCursor c4;
        CHECK (songStep (c4, 0, 1, [&loopOnly] (int i) { return loopOnly[i]; }, lenOf, local) == kSongNone);
    }

    // Fade: sube de 0 a 1 en el tiempo pedido y el fade out avisa al terminar.
    {
        Fader f;
        f.start (+1, 1.0f, 1000.0, true);
        CHECK (f.gain == 0.0f);
        for (int n = 0; n < 500; ++n) CHECK (! f.tick());
        CHECK (f.gain > 0.49f && f.gain < 0.51f);
        for (int n = 0; n < 600; ++n) f.tick();
        CHECK (f.gain == 1.0f && f.dir == 0);
        f.start (-1, 0.5f, 1000.0, false);
        int endedAt = -1;
        for (int n = 0; n < 1000; ++n) if (f.tick()) { endedAt = n; break; }
        CHECK (endedAt >= 498 && endedAt <= 501);
        CHECK (f.gain == 0.0f);
        // Todos los instrumentos tienen grupo de cancelar válido.
        for (int i = 0; i < kNumInst; ++i) CHECK (kCancelGroupOf[i] >= 0 && kCancelGroupOf[i] < kCancelGroups);
    }

    // MIDI: mapa por defecto, aprender con intercambio y modos de velocidad.
    {
        MidiMap m;
        CHECK (m.instFor (36) == (int) Inst::Kick);
        CHECK (m.instFor (38) == (int) Inst::Snare);
        CHECK (m.instFor (0) == -1);
        m.assign ((int) Inst::Kick, 38);                 // la caja tenía 38: se intercambian
        CHECK (m.instFor (38) == (int) Inst::Kick);
        CHECK (m.instFor (36) == (int) Inst::Snare);
        m.assign ((int) Inst::Cowbell, 99);
        CHECK (m.instFor (99) == (int) Inst::Cowbell && m.instFor (56) == -1);
        m.reset();
        CHECK (m.instFor (36) == (int) Inst::Kick && m.instFor (56) == (int) Inst::Cowbell);

        float lv; bool ac;
        midiHit (64, VelDynamic, 100, false, lv, ac);   CHECK (! ac && lv > 0.5f && lv < 0.51f);
        midiHit (110, VelDynamic, 100, false, lv, ac);  CHECK (ac);
        midiHit (64, VelDynamic, 100, true, lv, ac);    CHECK (ac);
        midiHit (20, VelTwoLevels, 100, false, lv, ac); CHECK (! ac && lv == 1.0f);
        midiHit (127, VelFixed, 100, false, lv, ac);    CHECK (! ac && lv == 1.0f);
        midiHit (1, VelFixed, 100, true, lv, ac);       CHECK (ac);
    }

    // Exportar: 2 compases de un patrón con bombo en 0 y 8 dan 4 bombos en negras 0, 2, 4 y 6.
    {
        Pattern p; p.length.store (16); p.stepsPerBeat.store (4);
        p.set (VarA, (int) Inst::Kick, 0, true);
        p.set (VarA, (int) Inst::Kick, 8, true);
        PlayOptions o;
        std::vector<double> times;
        renderSteps (p, o, 32, [&] (long, double ppq, const bool (&ev)[kRows]) { if (ev[(int) Inst::Kick]) times.push_back (ppq); });
        CHECK (times.size() == 4);
        if (times.size() == 4) CHECK (times[0] == 0.0 && times[1] == 2.0 && times[2] == 4.0 && times[3] == 6.0);
        // Con swing, los pasos impares se retrasan.
        o.swing = 0.66f;
        p.set (VarA, (int) Inst::HiHat, 1, true);
        double hh = -1;
        renderSteps (p, o, 2, [&] (long, double ppq, const bool (&ev)[kRows]) { if (ev[(int) Inst::HiHat]) hh = ppq; });
        CHECK (hh > 0.25 + 0.07 && hh < 0.25 + 0.09);
    }

    std::printf (failures ? "%d fallos\n" : "OK: todas las pruebas pasan\n", failures);
    return failures ? 1 : 0;
}
