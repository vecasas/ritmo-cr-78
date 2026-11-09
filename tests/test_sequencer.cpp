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

    std::printf (failures ? "%d fallos\n" : "OK: todas las pruebas pasan\n", failures);
    return failures ? 1 : 0;
}
