#pragma once
// Utilidades DSP independientes de JUCE (se pueden probar fuera del plugin).
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace r78
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline float semitonesToRatio (float st) { return std::pow (2.0f, st / 12.0f); }

// Coeficiente para una envolvente exponencial con constante de tiempo tau (s).
inline float expCoef (float tauSeconds, float sr)
{
    return tauSeconds <= 0.0f ? 0.0f : std::exp (-1.0f / (tauSeconds * sr));
}

// Ruido blanco rápido (xorshift32), salida en [-1, 1].
struct Noise
{
    uint32_t s = 0x12345678u;
    explicit Noise (uint32_t seed = 0x12345678u) : s (seed ? seed : 1u) {}
    float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (int32_t) s * (1.0f / 2147483648.0f);
    }
};

// Filtro de estado variable TPT (Zavalishin): LP, BP y HP estables al modular.
struct Svf
{
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;

    void set (float cutoff, float q, float sr)
    {
        cutoff = std::clamp (cutoff, 10.0f, sr * 0.49f);
        g = std::tan (kPi * cutoff / sr);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    void process (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

// Onda cuadrada con PolyBLEP (menos aliasing) para los sonidos metálicos.
struct BlepSquare
{
    float phase = 0, inc = 0;
    void setFreq (float f, float sr) { inc = f / sr; }
    static float blep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    float next()
    {
        float v = phase < 0.5f ? 1.0f : -1.0f;
        v += blep (phase, inc);
        float p2 = phase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        v -= blep (p2, inc);
        phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        return v;
    }
};

inline float softClip (float x) { return std::tanh (x); }
} // namespace r78
