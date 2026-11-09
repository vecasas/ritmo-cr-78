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
    float extra = 0.5f; // control propio de cada instrumento; 0.5 = sonido original
    float tune  = 0.0f; // semitonos, -12..+12
    float decay = 0.5f; // 0..1  (0.5 = valor de fábrica; mapea a x0.25 .. x4)
    float tone  = 0.5f; // 0..1  (brillo / mezcla, según la voz)
    float pan   = 0.0f; // -1..1
    bool  send  = false; // envía la voz a la cadena de efectos
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
        golpe = 2.0f * p.extra;   // "Golpe": caída de tono y clic de ataque
        lp.set (1500.0f + 9000.0f * p.tone, 0.7f, sr);
        lp.reset();
        // Ataque suave de ~1 ms como el de un filtro en T excitado por pulso.
        att = 0.0f;
        attInc = 1.0f / (0.0008f * sr);
    }
    float render() override
    {
        const float f = f0 * (1.0f + drop * golpe * penv);
        phase += f / sr;
        if (phase >= 1.0f) phase -= 1.0f;
        att = std::min (1.0f, att + attInc);
        float s = std::sin (kTwoPi * phase) * env * att;
        // Clic de ataque (ruido muy corto filtrado), controlado por tone.
        lp.process (noise.next() * clickEnv);
        s += lp.lp * click * golpe * (0.3f + p.tone);
        env *= envK; penv *= pK; clickEnv *= cK;
        return softClip (s * 1.3f);
    }

private:
    float base, tau, drop, dropTau, click;
    float phase = 0, env = 0, penv = 0, clickEnv = 0, envK = 0, pK = 0, cK = 0, f0 = 0;
    float att = 0, attInc = 0, golpe = 1.0f;
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
        f1 = 305.0f * r; f2 = 470.0f * r;
        ph1 = ph2 = 0;
        e1 = e2 = 1.0f; en = 1.0f; spike = 1.0f;
        k1 = expCoef (0.012f, sr); k2 = expCoef (0.008f, sr);
        kn = expCoef (0.012f, sr);
        ks = expCoef (0.004f, sr);
        hold = (long) (0.045f * ds * sr);
        hp.set (1500.0f + 2000.0f * p.tone, 0.6f, sr);
        bp.set (5200.0f * r, 1.1f, sr);
        lp.set (7500.0f + 3000.0f * p.tone, 0.7f, sr);
        hp.reset(); bp.reset(); lp.reset();
    }
    float render() override
    {
        ph1 += f1 / sr; if (ph1 >= 1) ph1 -= 1;
        ph2 += f2 / sr; if (ph2 >= 1) ph2 -= 1;
        const float body = std::sin (kTwoPi * ph1) * e1 * 0.8f + std::sin (kTwoPi * ph2) * e2 * 0.5f;
        lp.process (noise.next());
        hp.process (lp.lp);
        bp.process (hp.hp);
        // Ruido con golpe inicial y meseta sostenida que se corta (como el original).
        const float n = (hp.hp * 0.6f + bp.bp * 0.8f) * (0.33f * en + 0.67f * spike);
        e1 *= k1; e2 *= k2; spike *= ks;
        if (t > hold) en *= kn;
        // "Bordón": reparte entre el cuerpo tonal y el ruido de la bordonera.
        const float wn = 2.0f * p.extra, wb = 2.0f * (1.0f - p.extra);
        return softClip ((body * 0.22f * wb + n * 1.1f * wn) * 1.6f);
    }

private:
    float f1 = 0, f2 = 0, ph1 = 0, ph2 = 0, e1 = 0, e2 = 0, en = 0, k1 = 0, k2 = 0, kn = 0, spike = 0, ks = 0;
    long hold = 0;
    Svf hp, bp, lp;
    Noise noise { 0x51A2E1u };
};

class RimVoice : public Voice
{
protected:
    void onTrigger() override
    {
        const float r = semitonesToRatio (p.tune);
        f1 = 1400.0f * r; f2 = 160.0f * r;
        ph1 = ph2 = 0; e1 = e2 = 1.0f;
        const float ds = decayScale (p.decay);
        k1 = expCoef (0.0016f * ds, sr); k2 = expCoef (0.0025f * ds, sr);
        hp.set (40.0f + 200.0f * p.tone, 0.7f, sr);
        hp.reset();
    }
    float render() override
    {
        ph1 += f1 / sr; if (ph1 >= 1) ph1 -= 1;
        ph2 += f2 / sr; if (ph2 >= 1) ph2 -= 1;
        const float s = std::sin (kTwoPi * ph1) * e1 * (0.5f + 0.5f * p.tone)
                      + std::sin (kTwoPi * ph2) * e2 * 2.0f * p.extra;   // "Cuerpo": parte grave del golpe
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
    MetalVoice (float baseHz, float tauS, float hpHz, float bpHz, float noiseMix, float bpQ = 1.2f)
        : base (baseHz), tau (tauS), hpF (hpHz), bpF (bpHz), nMix (noiseMix), q (bpQ) {}

protected:
    void onTrigger() override
    {
        const float r = semitonesToRatio (p.tune);
        bank.set (base * r, sr);
        env = 1.0f;
        k = expCoef (tau * decayScale (p.decay), sr);
        const float bright = 0.6f + 0.8f * p.tone;
        bp.set (bpF * r * bright, q * (0.4f + 1.2f * p.extra), sr);   // "Resonancia"
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
    float base, tau, hpF, bpF, nMix, q;
    float env = 0, k = 0;
    MetalBank bank;
    Svf bp, hp;
    Noise noise { 0xC0FFEEu };
};

// Maracas / pandereta: ráfagas de ruido de agudos con ataque corto.
class ShakerVoice : public Voice
{
public:
    ShakerVoice (float hpHz, float tauS, float attackS, float metalAmt, float holdS = 0.0f, float lpHz = 20000.0f)
        : hpF (hpHz), tau (tauS), attack (attackS), metal (metalAmt), holdTime (holdS), lpF (lpHz) {}

protected:
    void onTrigger() override
    {
        const float r = semitonesToRatio (p.tune);
        hp.set (hpF * r * (0.7f + 0.6f * p.tone), 0.8f, sr);
        lp.set (std::min (lpF * r * (0.7f + 0.6f * p.tone), sr * 0.45f), 0.8f, sr);
        bank.set (1390.0f * r, sr);
        env = 1.0f;
        k = expCoef (tau * decayScale (p.decay), sr);
        // "Sacudida": ataque más lento y meseta más larga hacia arriba.
        const float sac = 0.25f + 1.5f * p.extra;
        attInc = 1.0f / std::max (1.0f, attack * sac * sr);
        att = 0;
        hold = (long) (holdTime * 2.0f * p.extra * decayScale (p.decay) * sr);
    }
    float render() override
    {
        att = std::min (1.0f, att + attInc);
        float src = noise.next();
        if (metal > 0) src = src * (1.0f - metal) + src * bank.next() * metal * 2.0f;
        hp.process (src);
        lp.process (hp.hp);
        const float s = lp.lp * env * att;
        if (t >= hold) env *= k;   // meseta sostenida y luego caída
        return softClip (s * 1.8f);
    }

private:
    float hpF, tau, attack, metal, holdTime, lpF;
    long hold = 0;
    float env = 0, k = 0, att = 0, attInc = 0;
    Svf hp, lp;
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
        bp.set (6000.0f * r * (0.7f + 0.6f * p.tone), 4.0f, sr);
        bp.reset();
        len = 0.27f * decayScale (p.decay);
        rate = 25.0f * r * (0.5f + p.extra);   // "Raspado": dientes más lentos o más rápidos
        lfo = 0;
    }
    float render() override
    {
        const float time = secs();
        if (time > len + 0.03f) return 0.0f;
        lfo += rate / sr; if (lfo >= 1) lfo -= 1;
        // Cada diente: raspado sostenido con un hueco breve al final.
        const float scrape = lfo < 0.72f ? 1.0f - 0.4f * lfo : 0.12f;
        const float fade = time < len ? 1.0f : std::max (0.0f, 1.0f - (time - len) / 0.03f);
        bp.process (noise.next());
        return softClip (bp.bp * scrape * fade * 2.5f);
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
        a.setFreq (775.0f * r, sr);
        b.setFreq (1160.0f * r * (1.0f + (p.extra - 0.5f) * 0.3f), sr);   // "Desafinar"
        bp.set (820.0f * r * (0.7f + 0.6f * p.tone), 2.5f, sr);
        bp.reset();
        e1 = e2 = 1.0f;
        k1 = expCoef (0.006f, sr);
        k2 = expCoef (0.022f * decayScale (p.decay), sr);
    }
    float render() override
    {
        bp.process (a.next() + 0.45f * b.next());
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

// Nombre del control propio de cada instrumento (en el orden de Inst).
inline constexpr std::array<const char*, kNumInst> kExtraName {
    "Golpe", "Bordón", "Cuerpo", "Resonancia", "Resonancia", "Resonancia", "Golpe", "Golpe", "Golpe", "Golpe",
    "Sacudida", "Raspado", "Sacudida", "Desafinar"
};

inline std::unique_ptr<Voice> makeVoice (Inst i)
{
    switch (i)
    {
        // Afinaciones y tiempos medidos sobre grabaciones de referencia de la máquina original.
        case Inst::Kick:       return std::make_unique<ResonantVoice> (60.0f, 0.030f, 0.50f, 0.008f, 0.08f);
        case Inst::Snare:      return std::make_unique<SnareVoice>();
        case Inst::Rim:        return std::make_unique<RimVoice>();
        case Inst::HiHat:      return std::make_unique<MetalVoice> (330.0f, 0.008f, 6500.0f, 8500.0f, 0.5f);
        case Inst::Cymbal:     return std::make_unique<MetalVoice> (300.0f, 0.11f, 6500.0f, 8200.0f, 0.55f);
        case Inst::MetalBeat:  return std::make_unique<MetalVoice> (420.0f, 0.020f, 4000.0f, 5900.0f, 0.1f, 5.0f);
        case Inst::HiBongo:    return std::make_unique<ResonantVoice> (590.0f, 0.014f, 0.05f, 0.004f, 0.10f);
        case Inst::LoBongo:    return std::make_unique<ResonantVoice> (389.0f, 0.010f, 0.05f, 0.004f, 0.25f);
        case Inst::LoConga:    return std::make_unique<ResonantVoice> (195.0f, 0.050f, 0.06f, 0.008f, 0.10f);
        case Inst::Claves:     return std::make_unique<ResonantVoice> (2450.0f, 0.022f, 0.0f, 0.001f, 0.05f);
        case Inst::Maracas:    return std::make_unique<ShakerVoice> (7500.0f, 0.016f, 0.004f, 0.0f);
        case Inst::Guiro:      return std::make_unique<GuiroVoice>();
        case Inst::Tambourine: return std::make_unique<ShakerVoice> (3800.0f, 0.020f, 0.002f, 0.6f, 0.11f, 6800.0f);
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
        float sl, sr;
        process (l, r, sl, sr);
        l += sl; r += sr;
    }

    // Versión con dos buses: seco (l/r) y envío a efectos (sendL/sendR).
    void process (float& l, float& r, float& sendL, float& sendR)
    {
        process (l, r, sendL, sendR, nullptr, nullptr, 0);
    }

    // Con salidas extra: un instrumento con outBus = k (1..numAux) va en seco a
    // auxL[k-1]/auxR[k-1], sin efectos. Si esa salida no existe, va a la principal.
    void process (float& l, float& r, float& sendL, float& sendR, float* auxL, float* auxR, int numAux)
    {
        l = r = sendL = sendR = 0;
        for (int k = 0; k < numAux; ++k) auxL[k] = auxR[k] = 0;
        for (int i = 0; i < kNumInst; ++i)
        {
            auto& v = voices[(size_t) i];
            if (! v->isActive()) continue;
            const float s = v->process();
            const float a = (v->p.pan + 1.0f) * 0.25f * kPi;
            const float sl = s * std::cos (a), sr = s * std::sin (a);
            const int bus = outBus[(size_t) i];
            if (bus > 0 && bus <= numAux) { auxL[bus - 1] += sl; auxR[bus - 1] += sr; }
            else if (v->p.send)           { sendL += sl; sendR += sr; }
            else                          { l += sl; r += sr; }
        }
    }

    // Salida de cada instrumento: 0 = principal, 1..7 = salidas extra 2..8.
    std::array<int, kNumInst> outBus {};

private:
    std::array<std::unique_ptr<Voice>, kNumInst> voices;
};
} // namespace r78
