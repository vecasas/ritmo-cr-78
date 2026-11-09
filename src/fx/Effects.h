#pragma once
// Efectos y procesado de máster de Ritmo CR-78 (sin dependencias de JUCE).
// Cadena de envío: Overdrive -> Flanger -> Delay -> Reverb con gate.
// Máster: compresor -> EQ gráfico de 6 bandas.

#include "../dsp/DspUtils.h"
#include <algorithm>
#include <array>
#include <vector>

namespace r78
{
// Línea de retardo con lectura fraccionaria.
struct DelayLine
{
    std::vector<float> buf;
    int w = 0;
    void resize (int n) { buf.assign ((size_t) std::max (4, n), 0.0f); w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
    // d en muestras (>= 1)
    float read (float d) const
    {
        const int n = (int) buf.size();
        d = std::clamp (d, 1.0f, (float) (n - 2));
        float pos = (float) w - d;
        while (pos < 0) pos += (float) n;
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % n;
        const float f = pos - (float) i0;
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * f;
    }
};

// ---------------------------------------------------------------- Overdrive
struct Overdrive
{
    float drive = 0.4f, tone = 0.5f, mix = 1.0f;
    int mode = 0;   // 0 transistor (asimétrico), 1 válvula (suave y simétrico), 2 fuzz (recorte duro)
    Svf lpL, lpR;
    float sr = 44100;
    void prepare (float s) { sr = s; lpL.reset(); lpR.reset(); }
    void process (float& l, float& r)
    {
        const float g = 1.0f + drive * drive * 30.0f;
        const float comp = 1.0f / std::tanh (std::min (g, 3.0f));
        lpL.set (800.0f + 11000.0f * tone, 0.7f, sr);
        lpR.set (800.0f + 11000.0f * tone, 0.7f, sr);
        // Pequeña asimetría para armónicos pares, como un transistor saturando.
        auto shape = [&] (float x) {
            switch (mode)
            {
                case 1: { const float y = g * 0.6f * x; return y / (1.0f + std::abs (y)) * (1.0f + 1.0f / (0.6f * g)); }
                case 2: { const float y = std::clamp (g * 2.0f * x, -1.0f, 1.0f); return y * 0.8f; }
                default: return (std::tanh (g * x + 0.15f) - std::tanh (0.15f)) * comp;
            }
        };
        lpL.process (shape (l)); lpR.process (shape (r));
        l = l + (lpL.lp * 0.7f - l) * mix;
        r = r + (lpR.lp * 0.7f - r) * mix;
    }
};

// ---------------------------------------------------------------- Flanger
struct Flanger
{
    float rate = 0.3f, depth = 0.6f, feedback = 0.4f, mix = 0.5f;
    DelayLine dl, dr;
    float sr = 44100, phase = 0, fbL = 0, fbR = 0;
    int mode = 0;   // 0 flanger, 1 chorus (retardo más largo, sin realimentación)
    void prepare (float s) { sr = s; dl.resize ((int) (s * 0.04f)); dr.resize ((int) (s * 0.04f)); phase = 0; fbL = fbR = 0; }
    void process (float& l, float& r)
    {
        const bool chorus = mode == 1;
        const float hz = chorus ? 0.1f + rate * rate * 2.0f : 0.03f + rate * rate * 5.0f;
        phase += hz / sr; if (phase >= 1) phase -= 1;
        auto tri = [] (float p) { return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; };
        const float base = (chorus ? 0.012f : 0.0006f) * sr;
        const float span = (chorus ? 0.0005f + 0.009f * depth : 0.0004f + 0.0068f * depth) * sr;
        const float dL = base + span * 0.5f * (1.0f + tri (phase));
        float pr = phase + 0.25f; if (pr >= 1) pr -= 1;           // canal derecho desfasado 90°
        const float dR = base + span * 0.5f * (1.0f + tri (pr));
        const float fb = chorus ? 0.0f : feedback * 0.92f;
        const float yl = dl.read (dL), yr = dr.read (dR);
        dl.push (l + yl * fb); dr.push (r + yr * fb);
        l = l * (1.0f - 0.5f * mix) + yl * mix * 0.7f;
        r = r * (1.0f - 0.5f * mix) + yr * mix * 0.7f;
    }
};

// ---------------------------------------------------------------- Delay
struct TempoDelay
{
    float timeBeats = 0.75f, feedback = 0.35f, damp = 0.4f, mix = 0.3f;
    float freeMs = 0.0f;   // > 0: tiempo libre en ms en lugar de seguir el tempo
    bool pingPong = true;
    DelayLine dl, dr;
    Svf lpL, lpR;
    float sr = 44100, smoothed = 0;
    void prepare (float s)
    {
        sr = s;
        dl.resize ((int) (s * 3.1f)); dr.resize ((int) (s * 3.1f));
        lpL.reset(); lpR.reset(); smoothed = 0;
    }
    void process (float& l, float& r, double bpm)
    {
        const float secs = freeMs > 0 ? freeMs * 0.001f : (float) (timeBeats * 60.0 / std::max (20.0, bpm));
        const float target = std::min (secs * sr, sr * 3.0f);
        smoothed = smoothed <= 0 ? target : smoothed + (target - smoothed) * 0.0005f;
        lpL.set (1200.0f + 14000.0f * (1.0f - damp), 0.6f, sr);
        lpR.set (1200.0f + 14000.0f * (1.0f - damp), 0.6f, sr);
        lpL.process (dl.read (smoothed)); lpR.process (dr.read (smoothed));
        const float yl = lpL.lp, yr = lpR.lp, fb = feedback * 0.95f;
        if (pingPong) { dl.push ((l + r) * 0.5f + yr * fb); dr.push (yl * fb); }
        else          { dl.push (l + yl * fb); dr.push (r + yr * fb); }
        l += yl * mix; r += yr * mix;
    }
};

// ---------------------------------------------------------------- Reverb con gate
// Red de retardos con realimentación (FDN 8x8 Householder) más un gate que corta
// la cola un tiempo después del último golpe: el sonido "gated" de los 80.
struct GatedReverb
{
    float size = 0.7f, gateMs = 250.0f, mix = 0.35f;
    bool gateOn = true;
    int mode = 0;   // 0 sala, 1 placa (más brillante y densa), 2 inversa (la cola crece y el gate la corta)
    static constexpr int N = 8;
    std::array<DelayLine, N> lines;
    std::array<float, N> lens {}, lpState {};
    DelayLine preL, preR;
    float sr = 44100, follower = 0, gateGain = 0;
    long sinceHit = 1 << 30;

    void prepare (float s)
    {
        sr = s;
        static constexpr float baseMs[N] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 73.3f };
        for (int i = 0; i < N; ++i) { lens[(size_t) i] = baseMs[i] * 0.001f * s; lines[(size_t) i].resize ((int) (lens[(size_t) i] * 1.6f) + 8); }
        preL.resize ((int) (s * 0.03f)); preR.resize ((int) (s * 0.03f));
        lpState.fill (0); follower = 0; gateGain = 0; sinceHit = 1 << 30;
    }
    void process (float& l, float& r)
    {
        // Detector del gate sobre la entrada.
        const float in = std::max (std::abs (l), std::abs (r));
        follower = std::max (in, follower * 0.9995f);
        if (in > 0.02f) sinceHit = 0; else ++sinceHit;
        const bool gateActive = gateOn || mode == 2;
        const float open = (! gateActive || sinceHit < (long) (gateMs * 0.001f * sr)) ? 1.0f : 0.0f;
        gateGain += (open - gateGain) * (open > gateGain ? 0.01f : 0.004f);

        const float scale = (0.6f + 0.8f * size) * (mode == 1 ? 0.7f : 1.0f);
        const float damping = mode == 1 ? 0.85f : 0.55f;
        const float fb = 0.70f + 0.27f * size;
        std::array<float, N> o {};
        float sum = 0;
        for (int i = 0; i < N; ++i)
        {
            o[(size_t) i] = lines[(size_t) i].read (lens[(size_t) i] * scale);
            lpState[(size_t) i] += (o[(size_t) i] - lpState[(size_t) i]) * damping; // amortiguación de agudos
            o[(size_t) i] = lpState[(size_t) i];
            sum += o[(size_t) i];
        }
        const float hh = sum * (2.0f / N);
        preL.push (l); preR.push (r);
        const float il = preL.read (0.012f * sr), ir = preR.read (0.012f * sr);
        for (int i = 0; i < N; ++i)
        {
            const float x = (o[(size_t) i] - hh) * fb + ((i & 1) ? ir : il) * 0.35f;
            lines[(size_t) i].push (x);
        }
        float wl = 0, wr = 0;
        for (int i = 0; i < N; ++i) ((i & 1) ? wr : wl) += o[(size_t) i];
        float swell = 1.0f;
        if (mode == 2)   // inversa: rampa creciente hasta que el gate cierra
        {
            const float x = std::min (1.0f, (float) sinceHit / std::max (1.0f, gateMs * 0.001f * sr));
            swell = 0.15f + 1.6f * x * x;
        }
        wl *= gateGain * 0.5f * swell; wr *= gateGain * 0.5f * swell;
        l += wl * mix; r += wr * mix;
    }
};

// ---------------------------------------------------------------- Compresor de máster
// Feed-forward con rodilla suave, detección estéreo enlazada y un toque de saturación
// en la ganancia de compensación, que le da el carácter "apretado".
struct Compressor
{
    float thresholdDb = -12.0f, ratio = 4.0f, attackMs = 5.0f, releaseMs = 120.0f, makeupDb = 0.0f;
    float sr = 44100, envDb = -120.0f;
    float gainReductionDb = 0.0f;   // para el medidor de la interfaz
    void prepare (float s) { sr = s; envDb = -120.0f; }
    void process (float& l, float& r)
    {
        const float peak = std::max (std::abs (l), std::abs (r));
        const float inDb = 20.0f * std::log10 (peak + 1.0e-6f);
        const float knee = 6.0f;
        float over = inDb - thresholdDb, grDb = 0;
        if (over > knee * 0.5f) grDb = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f) { const float x = over + knee * 0.5f; grDb = (1.0f - 1.0f / ratio) * x * x / (2.0f * knee); }
        const float a = expCoef (attackMs * 0.001f, sr), rel = expCoef (releaseMs * 0.001f, sr);
        envDb = grDb > envDb ? a * envDb + (1 - a) * grDb : rel * envDb + (1 - rel) * grDb;
        gainReductionDb = envDb;
        const float g = std::pow (10.0f, (makeupDb - envDb) / 20.0f);
        l = softClip (l * g * 0.9f) / 0.9f;
        r = softClip (r * g * 0.9f) / 0.9f;
    }
};

// ---------------------------------------------------------------- EQ gráfico de 6 bandas
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void peaking (float f, float q, float gainDb, float sr)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f), w = kTwoPi * f / sr;
        const float al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0;
    }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct GraphicEq
{
    static constexpr int kBands = 6;
    static constexpr std::array<float, kBands> freqs { 60.0f, 150.0f, 400.0f, 1000.0f, 3000.0f, 8000.0f };
    std::array<float, kBands> gainsDb {};
    std::array<Biquad, kBands> l, r;
    std::array<float, kBands> applied { 99, 99, 99, 99, 99, 99 };
    float sr = 44100;
    void prepare (float s) { sr = s; applied.fill (99); for (auto& b : l) b.z1 = b.z2 = 0; for (auto& b : r) b.z1 = b.z2 = 0; }
    void update()
    {
        for (int i = 0; i < kBands; ++i)
            if (std::abs (gainsDb[(size_t) i] - applied[(size_t) i]) > 1.0e-4f)
            {
                l[(size_t) i].peaking (freqs[(size_t) i], 1.1f, gainsDb[(size_t) i], sr);
                r[(size_t) i] = Biquad { l[(size_t) i].b0, l[(size_t) i].b1, l[(size_t) i].b2, l[(size_t) i].a1, l[(size_t) i].a2,
                                         r[(size_t) i].z1, r[(size_t) i].z2 };
                applied[(size_t) i] = gainsDb[(size_t) i];
            }
    }
    void process (float& L, float& R)
    {
        for (int i = 0; i < kBands; ++i)
        {
            if (std::abs (applied[(size_t) i]) < 0.01f) continue; // banda plana: no procesa
            L = l[(size_t) i].process (L); R = r[(size_t) i].process (R);
        }
    }
};

// ---------------------------------------------------------------- Cadena completa
struct FxChain
{
    Overdrive od; Flanger fl; TempoDelay dl; GatedReverb rv;
    bool odOn = false, flOn = false, dlOn = false, rvOn = true;
    void prepare (float sr) { od.prepare (sr); fl.prepare (sr); dl.prepare (sr); rv.prepare (sr); }
    void process (float& l, float& r, double bpm)
    {
        if (odOn) od.process (l, r);
        if (flOn) fl.process (l, r);
        if (dlOn) dl.process (l, r, bpm);
        if (rvOn) rv.process (l, r);
    }
};

struct MasterBus
{
    Compressor comp; GraphicEq eq;
    bool compOn = true, eqOn = true;
    void prepare (float sr) { comp.prepare (sr); eq.prepare (sr); }
    void process (float& l, float& r)
    {
        if (compOn) comp.process (l, r); else comp.gainReductionDb = 0;
        if (eqOn) { eq.update(); eq.process (l, r); }
    }
};
} // namespace r78
