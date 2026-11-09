// Pruebas del secuenciador sin JUCE. Sale con código 1 si algo falla.
#include "../src/seq/Sequencer.h"
#include <cstdio>

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

    std::printf (failures ? "%d fallos\n" : "OK: todas las pruebas pasan\n", failures);
    return failures ? 1 : 0;
}
