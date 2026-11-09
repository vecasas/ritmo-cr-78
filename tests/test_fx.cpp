// Pruebas de los efectos sin JUCE: estabilidad, ausencia de NaN, gate y EQ plano.
#include "../src/fx/Effects.h"
#include "../src/dsp/Voices.h"
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main()
{
    using namespace r78;
    const float sr = 48000;

    // Cadena completa al máximo durante 10 s con golpes: sin NaN ni explosión.
    {
        FxChain fx; fx.prepare (sr);
        fx.odOn = fx.flOn = fx.dlOn = fx.rvOn = true;
        fx.od.drive = 1; fx.fl.feedback = 1; fx.dl.feedback = 1; fx.rv.size = 1; fx.rv.gateOn = false;
        MasterBus m; m.prepare (sr); m.eq.gainsDb.fill (12.0f);
        DrumKit kit; kit.setSampleRate (sr);
        float peak = 0; bool finite = true;
        for (int n = 0; n < (int) sr * 10; ++n)
        {
            if (n % 12000 == 0) kit.trigger (n / 12000 % kNumInst, 1.0f, true);
            float l, r; kit.process (l, r);
            fx.process (l, r, 120.0); m.process (l, r);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        }
        CHECK (finite);
        CHECK (peak < 20.0f);
    }

    // El gate de la reverb corta la cola: tras el tiempo de gate queda silencio.
    {
        GatedReverb rv; rv.prepare (sr); rv.size = 1.0f; rv.gateMs = 200; rv.mix = 1.0f;
        float tailAfterGate = 0, tailBefore = 0;
        for (int n = 0; n < (int) (sr * 0.6f); ++n)
        {
            float l = n < 100 ? 1.0f : 0.0f, r = l;
            rv.process (l, r);
            const float t = n / sr;
            if (t > 0.05f && t < 0.15f) tailBefore = std::max (tailBefore, std::abs (l));
            if (t > 0.40f) tailAfterGate = std::max (tailAfterGate, std::abs (l));
        }
        CHECK (tailBefore > 0.01f);
        CHECK (tailAfterGate < 0.001f);
    }

    // EQ a 0 dB no cambia la señal.
    {
        GraphicEq eq; eq.prepare (sr); eq.update();
        float maxDiff = 0; Noise nz;
        for (int n = 0; n < 1000; ++n) { float x = nz.next(), l = x, r = x; eq.process (l, r); maxDiff = std::max (maxDiff, std::abs (l - x)); }
        CHECK (maxDiff < 1e-6f);
    }

    // El compresor reduce la ganancia de una señal fuerte y no la de una débil.
    {
        Compressor c; c.prepare (sr); c.thresholdDb = -20; c.ratio = 8; c.makeupDb = 0;
        for (int n = 0; n < 4800; ++n) { float l = 0.9f, r = 0.9f; c.process (l, r); }
        CHECK (c.gainReductionDb > 10.0f);
        Compressor q; q.prepare (sr); q.thresholdDb = -20; q.ratio = 8;
        for (int n = 0; n < 4800; ++n) { float l = 0.01f, r = 0.01f; q.process (l, r); }
        CHECK (q.gainReductionDb < 0.1f);
    }

    std::printf (failures ? "%d fallos\n" : "OK: pruebas de efectos pasan\n", failures);
    return failures ? 1 : 0;
}
