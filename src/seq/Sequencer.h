#pragma once
// Secuenciador de pasos de Ritmo CR-78 (sin dependencias de JUCE).
// Cada ritmo tiene tres variaciones (A, B y Fill). Cada variación tiene una fila
// por instrumento y una fila de acentos. La reproducción es sample-accurate y se
// puede enganchar a la posición del DAW (ppq) o a un reloj interno.

#include "../dsp/Voices.h"
#include <atomic>
#include <string>
#include <vector>

namespace r78
{
constexpr int kMaxSteps = 32;
constexpr int kAccentRow = kNumInst;      // fila extra para acentos
constexpr int kRows = kNumInst + 1;
enum Variation { VarA = 0, VarB = 1, VarFill = 2, kNumVars = 3 };

// Patrón editable desde la interfaz y leído desde el hilo de audio sin bloqueos.
struct Pattern
{
    std::atomic<int> length { 16 };       // pasos por compás (1..32)
    std::atomic<int> stepsPerBeat { 4 };  // 4 = semicorcheas, 3 = tresillos
    std::atomic<uint8_t> cells[kNumVars][kRows][kMaxSteps] {};

    bool get (int var, int row, int step) const { return cells[var][row][step].load (std::memory_order_relaxed) != 0; }
    void set (int var, int row, int step, bool on) { cells[var][row][step].store (on ? 1 : 0, std::memory_order_relaxed); }
    void clear()
    {
        for (auto& v : cells) for (auto& r : v) for (auto& c : r) c.store (0, std::memory_order_relaxed);
    }
};

// Definición de un ritmo de fábrica en texto: "bd x...x...;sd ....x...;ac x...x...".
struct RhythmDef
{
    const char* name;
    const char* style;
    float bpm;
    int length;
    int stepsPerBeat;
    const char* a;
    const char* b;
    const char* fill;
};

const std::vector<RhythmDef>& factoryRhythms();

inline int rowForId (const std::string& id)
{
    if (id == "ac") return kAccentRow;
    for (int i = 0; i < kNumInst; ++i) if (id == kInstInfo[(size_t) i].id) return i;
    return -1;
}

inline void parseVariation (Pattern& p, int var, const char* text)
{
    if (! text) return;
    std::string s (text);
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t end = s.find (';', pos);
        if (end == std::string::npos) end = s.size();
        std::string line = s.substr (pos, end - pos);
        pos = end + 1;
        const auto sp = line.find_first_not_of (' ');
        if (sp == std::string::npos) continue;
        line = line.substr (sp);
        const auto gap = line.find (' ');
        if (gap == std::string::npos) continue;
        const int row = rowForId (line.substr (0, gap));
        if (row < 0) continue;
        const std::string steps = line.substr (line.find_first_not_of (' ', gap));
        for (int i = 0; i < (int) steps.size() && i < kMaxSteps; ++i)
            p.set (var, row, i, steps[(size_t) i] == 'x' || steps[(size_t) i] == 'X');
    }
}

inline void loadRhythm (Pattern& p, const RhythmDef& d)
{
    p.clear();
    p.length.store (d.length);
    p.stepsPerBeat.store (d.stepsPerBeat);
    parseVariation (p, VarA, d.a);
    parseVariation (p, VarB, d.b ? d.b : d.a);
    parseVariation (p, VarFill, d.fill ? d.fill : d.a);
}

// Opciones de reproducción que cambian en tiempo real.
struct PlayOptions
{
    float swing = 0.5f;      // 0.5 = recto, 0.75 = shuffle marcado
    int variation = 0;       // 0 = A, 1 = B, 2 = A/B alternos por compás
    int fillEvery = 0;       // 0 = nunca, si no cada N compases
    float accentLevel = 1.0f;
};

// Motor de reproducción. Se avanza muestra a muestra a partir de una posición en
// negras (ppq). Devuelve qué instrumentos suenan en cada paso por callback.
class StepClock
{
public:
    // Recoloca el cursor (al arrancar o si el DAW salta).
    void locate (double ppq, int stepsPerBeat)
    {
        spb = stepsPerBeat;
        nextStep = (long) std::ceil (ppq * spb - 1.0e-9);
        if (nextStep < 0) nextStep = 0;
    }

    // Instante (en ppq) en el que suena el paso global g, con swing aplicado a los pasos impares.
    double stepTime (long g, float swing) const
    {
        const double base = (double) g / spb;
        if (g % 2 == 1 && spb % 2 == 0)
            return base + (swing - 0.5) * 2.0 / spb;
        return base;
    }

    // Si en ppq ya toca el siguiente paso, lo devuelve y avanza; si no, -1.
    long poll (double ppq, float swing)
    {
        if (ppq + 1.0e-9 >= stepTime (nextStep, swing)) return nextStep++;
        return -1;
    }

    long peekNext() const { return nextStep; }

private:
    int spb = 4;
    long nextStep = 0;
};

// Decide qué variación suena en un compás dado.
inline int variationForBar (long bar, const PlayOptions& o)
{
    if (o.fillEvery > 0 && (bar + 1) % o.fillEvery == 0) return VarFill;
    if (o.variation == 2) return (bar % 2 == 0) ? VarA : VarB;
    return o.variation == 1 ? VarB : VarA;
}
} // namespace r78
