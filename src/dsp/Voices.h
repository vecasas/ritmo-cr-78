#pragma once
// Voces de percusión sintetizadas de Ritmo CR-78.
// Diseño propio inspirado en la arquitectura de las cajas de ritmos analógicas
// de finales de los 70: resonadores con decaimiento exponencial (tipo puente en T),
// ruido filtrado y osciladores cuadrados para los sonidos metálicos.
// Sin dependencias de JUCE, para poder probarlo y renderizarlo fuera del plugin.

#include "DspUtils.h"
#include <array>
#include <memory>
#include <string>

namespace r78
{
// Parámetros comunes a todas las voces (rango normalizado salvo tune).
struct VoiceParams
{
    float level = 0.8f; // 0..1
    float tune  = 0.0f; // semitonos, -12..+12
    float decay = 0.5f; // 0..1  (0.5 = valor de fábrica; mapea a x0.25 .. x4)
    float tone  = 0.5f; // 0..1  (brillo / mezcla, según la voz)
    float pan   = 0.0f; // -1..1
};

inline float decayScale (float d) { return std::pow (4.0f, (d - 0.5f) * 2.0f); }

class Voice
{
public:
    virtual ~Voice() = default;
    virtual void setSampleRate (float newSr) { sr = newSr; }
    // vel 0..1; accent añade pegada extra.
    void trigger (float vel, bool accent)
    {
        amp = vel * (accent ? 1.0f : 0.72f);
        t = 0;
        active = true;
        onTrigger();
    }
    float process()
    {
        if (! active) return 0.0f;
        const float s = render() * amp * p.level;
        ++t;
        // Silencio prolongado = voz libre (ahorra CPU).
        if (std::abs (s) < 1.0e-5f) { if (++quiet > (int) (sr * 0.05f)) active = false; }
        else quiet = 0;
        return s;
    }
    bool isActive() const { return active; }
    void choke() { active = false; }

    VoiceParams p;

protected:
    virtual void onTrigger() = 0;
    virtual float render() = 0;
    float secs() const { return (float) t / sr; }

    float sr = 44100.0f;
    float amp = 0.0f;
    long t = 0;
    int quiet = 0;
    bool active = false;
};

// Oscilador senoidal percutido con caída de tono: base de bombo, bongós, congas, claves.
class ResonantVoice : public Voice
{
public:
    ResonantVoice (float baseHz, float tauS, float pitchDrop, float pitchTau, float clickAmt)
        : base (baseHz), tau (tauS), drop (pitchDrop), dropTau (pitchTau), click (clickAmt) {}

protected:
    void onTrigger() override
    {
        phase = 0.0f;
        env = 1.0f;
        penv = 1.0f;
        clickEnv = 1.0f;
        const float ds = decayScale (p.decay);
        envK = expCoef (tau * ds, sr);
        pK = expCoef (dropTau, sr);
        cK = expCoef (0.0015f, sr);
        f0 = base * semitonesToRatio (p.tune);
        lp.set (1500.0f + 9000.0f * p.tone, 0.7f, sr);
        lp.reset();
        // Ataque suave de ~1 ms como el de un filtro en T excitado por pulso.
        att = 0.0f;
        attInc = 1.0f / (0.0008f * sr);
    }
    float render() override
    {
        const float f = f0 * (1.0f + drop * penv);
        phase += f / sr;
        if (phase >= 1.0f) phase -= 1.0f;
        att = std::min (1.0f, att + attInc);
        float s = std::sin (kTwoPi * phase) * env * att;
        // Clic de ataque (ruido muy corto filtrado), controlado por tone.
        lp.process (noise.next() * clickEnv);
        s += lp.lp * click * (0.3f + p.tone);
        env *= envK; penv *= pK; clickEnv *= cK;
        return softClip (s * 1.3f);
    }

private:
    float base, tau, drop, dropTau, click;
    float phase = 0, env = 0, penv = 0, clickEnv = 0, envK = 0, pK = 0, cK = 0, f0 = 0;
    float att = 0, attInc = 0;
    Svf lp;
    Noise noise { 0xBADA55u };
};

class SnareVoice : public Voice
{
protected:
    void onTrigger() override
    {
        const float ds = decayScale (p.decay);
        const float r = semitonesToRatio (p.tune);
        f1 = 185.0f * r; f2 = 330.0f * r;
        ph1 = ph2 = 0;
        e1 = e2 = 1.0f; en = 1.0f;
        k1 = expCoef (0.045f, sr); k2 = expCoef (0.025f, sr);
        kn = expCoef (0.075f * ds, sr);
        hp.set (1200.0f + 1500.0f * p.tone, 0.6f, sr);
        bp.set (5200.0f, 0.9f, sr);
        hp.reset(); bp.reset();
    }
    float render() override
    {
        ph1 += f1 / sr; if (ph1 >= 1) ph1 -= 1;
        ph2 += f2 / sr; if (ph2 >= 1) ph2 -= 1;
        const float body = std::sin (kTwoPi * ph1) * e1 * 0.8f + std::sin (kTwoPi * ph2) * e2 * 0.5f;
        hp.process (noise.next());
        bp.process (hp.hp);
        const float n = (hp.hp * 0.6f + bp.bp * 0.8f) * en;
        const float mix = 0.35f + 0.5f * p.tone; // tone = más bordón
        e1 *= k1; e2 *= k2; en *= kn;
        return softClip ((body * (1.0f - mix) + n * mix) * 1.6f);
    }

private:
    float f1 = 0, f2 = 0, ph1 = 0, ph2 = 0, e1 = 0, e2 = 0, en = 0, k1 = 0, k2 = 0, kn = 0;
    Svf hp, bp;
    Noise noise { 0x51A2E1u };
};

class RimVoice : public Voice
{
protected:
    void onTrigger() override
    {
        const float r = semitonesToRatio (p.tune);
        f1 = 1650.0f * r; f2 = 455.0f * r;
        ph1 = ph2 = 0; e1 = e2 = 1.0f;
        const float ds = decayScale (p.decay);
        k1 = expCoef (0.010f * ds, sr); k2 = expCoef (0.006f * ds, sr);
        hp.set (300.0f + 900.0f * p.tone, 0.7f, sr);
        hp.reset();
    }
    float render() override
    {
        ph1 += f1 / sr; if (ph1 >= 1) ph1 -= 1;
        ph2 += f2 / sr; if (ph2 >= 1) ph2 -= 1;
        const float s = std::sin (kTwoPi * ph1) * e1 + std::sin (kTwoPi * ph2) * e2 * 0.8f;
        hp.process (softClip (s * 3.0f));
        e1 *= k1; e2 *= k2;
        return hp.hp * 0.8f;
    }

private:
    float f1 = 0, f2 = 0, ph1 = 0, ph2 = 0, e1 = 0, e2 = 0, k1 = 0, k2 = 0;
    Svf hp;
};

// Banco de 6 osciladores cuadrados desafinados (frecuencias propias) para timbres metálicos.
struct MetalBank
{
    std::array<BlepSquare, 6> osc;
    static constexpr std::array<float, 6> ratios { 1.0f, 1.523f, 2.004f, 2.331f, 2.992f, 3.917f };
    void set (float baseHz, float sr)
    {
        for (size_t i = 0; i < osc.size(); ++i) osc[i].setFreq (baseHz * ratios[i], sr);
    }
    float next()
    {
        float s = 0;
        for (auto& o : osc) s += o.next();
        return s * (1.0f / 6.0f);
    }
};

// Charles / platillo / "metal beat": banco metálico + ruido, filtrados y con envolvente.
class MetalVoice : public Voice
{
public:
    MetalVoice (float baseHz, float tauS, float hpHz, float bpHz, float noiseMix)
        : base (baseHz), tau (tauS), hpF (hpHz), bpF (bpHz), nMix (noiseMix) {}

protected:
    void onTrigger() override
    {
        const float r = semitonesToRatio (p.tune);
        bank.set (base * r, sr);
        env = 1.0f;
        k = expCoef (tau * decayScale (p.decay), sr);
        const float bright = 0.6f + 0.8f * p.tone;
        bp.set (bpF * r * bright, 1.2f, sr);
        hp.set (hpF * r * bright, 0.7f, sr);
    }
    float render() override
    {
        const float src = bank.next() * (1.0f - nMix) + noise.next() * nMix;
        bp.process (src);
        hp.process (bp.bp);
        const float s = hp.hp * env;
        env *= k;
        return softClip (s * 2.2f);
    }

private:
    float base, tau, hpF, bpF, nMix;
    float env = 0, k = 0;
    MetalBank bank;
    Svf bp, hp;
    Noise noise { 0xC0FFEEu };
};

// Maracas / pandereta: ráfagas de ruido de agudos con ataque corto.
class ShakerVoice : public Voice
{
public:
    ShakerVoice (float hpHz, float tauS, float attackS, float metalAmt)
        : hpF (hpHz), tau (tauS), attack (attackS), metal (metalAmt) {}

protected:
    void onTrigger() override
    {
        const float r = semitonesToRatio (p.tune);
        hp.set (hpF * r * (0.7f + 0.6f * p.tone), 0.8f, sr);
        bank.set (1390.0f * r, sr);
        env = 1.0f;
        k = expCoef (tau * decayScale (p.decay), sr);
        attInc = 1.0f / std::max (1.0f, attack * sr);
        att = 0;
    }
    float render() override
    {
        att = std::min (1.0f, att + attInc);
        float src = noise.next();
        if (metal > 0) src = src * (1.0f - metal) + src * bank.next() * metal * 2.0f;
        hp.process (src);
        const float s = hp.hp * env * att;
        env *= k;
        return softClip (s * 1.8f);
    }

private:
    float hpF, tau, attack, metal;
    float env = 0, k = 0, att = 0, attInc = 0;
    Svf hp;
    MetalBank bank;
    Noise noise { 0x7A3B11u };
};

// Güiro: ruido en banda modulado por una rampa rápida (raspado) durante ~200 ms.
class GuiroVoice : public Voice
{
protected:
    void onTrigger() override
    {
        const float r = semitonesToRatio (p.tune);
        bp.set (3200.0f * r * (0.7f + 0.6f * p.tone), 2.5f, sr);
        bp.reset();
        len = 0.19f * decayScale (p.decay);
        rate = 38.0f * r;
        lfo = 0;
    }
    float render() override
    {
        const float time = secs();
        if (time > len + 0.03f) return 0.0f;
        lfo += rate / sr; if (lfo >= 1) lfo -= 1;
        const float scrape = std::pow (1.0f - lfo, 3.0f);
        const float fade = time < len ? 1.0f : std::max (0.0f, 1.0f - (time - len) / 0.03f);
        bp.process (noise.next());
        return softClip (bp.bp * scrape * fade * 4.0f);
    }

private:
    float len = 0, rate = 0, lfo = 0;
    Svf bp;
    Noise noise { 0x6A1E0u };
};

// Cencerro: dos cuadradas filtradas con envolvente de dos etapas.
class CowbellVoice : public Voice
{
protected:
    void onTrigger() override
    {
        const float r = semitonesToRatio (p.tune);
        a.setFreq (562.0f * r, sr); b.setFreq (838.0f * r, sr);
        bp.set (760.0f * r * (0.7f + 0.6f * p.tone), 2.0f, sr);
        bp.reset();
        e1 = e2 = 1.0f;
        k1 = expCoef (0.012f, sr);
        k2 = expCoef (0.11f * decayScale (p.decay), sr);
    }
    float render() override
    {
        bp.process (a.next() + b.next());
        const float s = bp.bp * (0.6f * e1 + 0.4f * e2);
        e1 *= k1; e2 *= k2;
        return softClip (s * 1.8f);
    }

private:
    BlepSquare a, b;
    Svf bp;
    float e1 = 0, e2 = 0, k1 = 0, k2 = 0;
};

// Identificadores de instrumento. El orden define el de la interfaz y el mezclador.
enum class Inst
{
    Kick, Snare, Rim, HiHat, Cymbal, MetalBeat, HiBongo, LoBongo, LoConga,
    Claves, Maracas, Guiro, Tambourine, Cowbell, Count
};
constexpr int kNumInst = (int) Inst::Count;

struct InstInfo
{
    const char* id;       // id corto para parámetros
    const char* name;     // nombre visible
    int midiNote;         // nota MIDI por defecto (mapa estilo GM)
};

inline constexpr std::array<InstInfo, kNumInst> kInstInfo { {
    { "bd", "Bombo",       36 },
    { "sd", "Caja",        38 },
    { "rs", "Aro",         37 },
    { "hh", "Charles",     42 },
    { "cy", "Platillo",    49 },
    { "mb", "Metal",       51 },
    { "hb", "Bongó agudo", 60 },
    { "lb", "Bongó grave", 61 },
    { "lc", "Conga",       64 },
    { "cl", "Claves",      75 },
    { "ma", "Maracas",     70 },
    { "gu", "Güiro",       73 },
    { "ta", "Pandereta",   54 },
    { "cb", "Cencerro",    56 },
} };

inline std::unique_ptr<Voice> makeVoice (Inst i)
{
    switch (i)
    {
        case Inst::Kick:       return std::make_unique<ResonantVoice> (52.0f, 0.16f, 0.55f, 0.012f, 0.25f);
        case Inst::Snare:      return std::make_unique<SnareVoice>();
        case Inst::Rim:        return std::make_unique<RimVoice>();
        case Inst::HiHat:      return std::make_unique<MetalVoice> (330.0f, 0.032f, 7000.0f, 9500.0f, 0.45f);
        case Inst::Cymbal:     return std::make_unique<MetalVoice> (300.0f, 0.55f, 5200.0f, 7800.0f, 0.55f);
        case Inst::MetalBeat:  return std::make_unique<MetalVoice> (420.0f, 0.07f, 2200.0f, 3600.0f, 0.15f);
        case Inst::HiBongo:    return std::make_unique<ResonantVoice> (410.0f, 0.055f, 0.08f, 0.006f, 0.15f);
        case Inst::LoBongo:    return std::make_unique<ResonantVoice> (285.0f, 0.075f, 0.08f, 0.006f, 0.15f);
        case Inst::LoConga:    return std::make_unique<ResonantVoice> (192.0f, 0.12f, 0.12f, 0.010f, 0.12f);
        case Inst::Claves:     return std::make_unique<ResonantVoice> (2450.0f, 0.022f, 0.0f, 0.001f, 0.05f);
        case Inst::Maracas:    return std::make_unique<ShakerVoice> (7500.0f, 0.016f, 0.004f, 0.0f);
        case Inst::Guiro:      return std::make_unique<GuiroVoice>();
        case Inst::Tambourine: return std::make_unique<ShakerVoice> (6500.0f, 0.10f, 0.002f, 0.6f);
        case Inst::Cowbell:    return std::make_unique<CowbellVoice>();
        case Inst::Count:      break;
    }
    return nullptr;
}

// Conjunto de las 14 voces con salida estéreo y mapa de notas MIDI.
class DrumKit
{
public:
    DrumKit()
    {
        for (int i = 0; i < kNumInst; ++i) voices[(size_t) i] = makeVoice ((Inst) i);
    }
    void setSampleRate (float sr) { for (auto& v : voices) v->setSampleRate (sr); }
    Voice& voice (int i) { return *voices[(size_t) i]; }

    void trigger (int inst, float vel, bool accent)
    {
        if (inst < 0 || inst >= kNumInst) return;
        // El charles y el platillo comparten circuito de corte en muchas máquinas
        // analógicas: no lo forzamos, pero el charles corta al "metal".
        voices[(size_t) inst]->trigger (vel, accent);
    }
    int instForNote (int note) const
    {
        for (int i = 0; i < kNumInst; ++i) if (kInstInfo[(size_t) i].midiNote == note) return i;
        return -1;
    }
    // Suma una muestra en l/r con panorama de potencia constante.
    void process (float& l, float& r)
    {
        l = r = 0;
        for (auto& v : voices)
        {
            if (! v->isActive()) continue;
            const float s = v->process();
            const float a = (v->p.pan + 1.0f) * 0.25f * kPi;
            l += s * std::cos (a);
            r += s * std::sin (a);
        }
    }

private:
    std::array<std::unique_ptr<Voice>, kNumInst> voices;
};
} // namespace r78
