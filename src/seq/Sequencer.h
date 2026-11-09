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
    int fillType = 0;        // ver FillType
    float accentLevel = 1.0f;
};

// Tipos de relleno para el compás de fill.
enum FillType
{
    FillPattern = 0,   // la variación Fill del propio ritmo
    FillSwapAB,        // toca la variación contraria (A <-> B)
    FillBreak,         // solo suena el primer paso; el resto del compás en silencio
    FillRollBongo,     // el ritmo sigue y se añade un redoble de bongó agudo en los 2 últimos tiempos
    FillRollSnare,     // igual, con la caja
    kNumFillTypes
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

// Calcula qué filas (instrumentos + acento) suenan en el paso global g.
// Devuelve la variación "visible" (para la interfaz).
inline int stepEvents (const Pattern& p, long g, const PlayOptions& o, bool (&out)[kRows])
{
    const int len = std::max (1, std::min (kMaxSteps, p.length.load()));
    const int spb = std::max (1, p.stepsPerBeat.load());
    const int step = (int) (g % len);
    const long bar = g / len;
    for (auto& b : out) b = false;

    const bool fillBar = o.fillEvery > 0 && (bar + 1) % o.fillEvery == 0;
    PlayOptions base = o; base.fillEvery = 0;
    int var = variationForBar (bar, base);

    if (fillBar)
    {
        switch (o.fillType)
        {
            case FillPattern: var = VarFill; break;
            case FillSwapAB:  var = (var == VarA) ? VarB : VarA; break;
            case FillBreak:
                if (step != 0) return var;
                break;
            default: break;
        }
    }
    for (int r = 0; r < kRows; ++r) out[r] = p.get (var, r, step);

    // Redoble: semicorcheas (o tresillos) en los dos últimos tiempos del compás.
    if (fillBar && (o.fillType == FillRollBongo || o.fillType == FillRollSnare) && step >= len - 2 * spb)
    {
        out[o.fillType == FillRollBongo ? (int) Inst::HiBongo : (int) Inst::Snare] = true;
        if (step == len - 2 * spb || step == len - spb) out[kAccentRow] = true;
    }
    return var;
}
// ---------------------------------------------------------------- Modo canción
// Una canción es una lista de pasos; cada paso es un patrón de usuario (0..98),
// kSongEnd (para la reproducción) o kSongLoop (vuelve al principio).
constexpr int kSongEnd = -1, kSongLoop = -2, kSongNone = -3;

struct SongCursor
{
    int pos = -1;        // paso de canción actual (-1 = sin empezar)
    long barStart = 0;   // paso global en que empezó el patrón actual
    void reset() { pos = -1; barStart = 0; }
};

// Avanza el cursor hasta el paso global g. Devuelve el patrón (entrada de la lista)
// que suena y su paso local, kSongEnd si hay que parar o kSongNone si la lista está vacía.
// entryAt(i) da la entrada i; lengthOf(patrón) da su longitud en pasos.
template <typename EntryAt, typename LengthOf>
int songStep (SongCursor& c, long g, int count, EntryAt entryAt, LengthOf lengthOf, long& local)
{
    if (count <= 0) return kSongNone;
    if (c.pos < 0) { c.pos = 0; c.barStart = g; }
    for (int guard = 0; guard < 2 * count + 2; ++guard)
    {
        if (c.pos >= count) c.pos = 0;                 // al final de la lista vuelve a empezar
        const int e = entryAt (c.pos);
        if (e == kSongLoop) { c.pos = 0; continue; }
        if (e == kSongEnd) return kSongEnd;
        const int len = lengthOf (e) < 1 ? 1 : lengthOf (e);
        if (g - c.barStart >= len) { c.barStart += len; ++c.pos; continue; }
        local = g - c.barStart;
        return e;
    }
    return kSongNone;
}
// ---------------------------------------------------------------- Cancelar voz
// Seis teclas que quitan grupos de instrumentos del ritmo sin tocar el patrón.
constexpr int kCancelGroups = 6;
inline constexpr int kCancelGroupOf[kNumInst] {
    0,          // bombo
    1, 1,       // caja, aro
    2, 2, 2,    // charles, platillo, metal
    3, 3, 3,    // bongó agudo, bongó grave, conga
    4,          // claves
    5, 5, 5,    // maracas, güiro, pandereta
    4           // cencerro
};

// ---------------------------------------------------------------- Fade
// Rampa lineal de volumen para entrar o salir del ritmo.
struct Fader
{
    float gain = 1.0f, step = 0.0f;
    int dir = 0;   // +1 subiendo, -1 bajando, 0 quieto

    void start (int d, float seconds, double sampleRate, bool fromSilence)
    {
        if (d > 0 && fromSilence) gain = 0.0f;
        dir = d;
        step = (float) (1.0 / ((seconds < 0.05f ? 0.05f : seconds) * sampleRate));
    }
    void reset() { gain = 1.0f; dir = 0; }
    // Avanza una muestra. Devuelve true en la muestra en que termina un fade out.
    bool tick()
    {
        if (dir > 0) { gain += step; if (gain >= 1.0f) { gain = 1.0f; dir = 0; } }
        else if (dir < 0) { gain -= step; if (gain <= 0.0f) { gain = 0.0f; dir = 0; return true; } }
        return false;
    }
};
// ---------------------------------------------------------------- Exportar a MIDI
// Recorre `steps` pasos del patrón con las opciones de reproducción y llama a
// emit(paso global, posición en negras con swing, eventos de la fila) en cada paso.
template <typename Emit>
void renderSteps (const Pattern& p, const PlayOptions& o, long steps, Emit emit)
{
    StepClock c;
    c.locate (0.0, p.stepsPerBeat.load() < 1 ? 4 : p.stepsPerBeat.load());
    for (long g = 0; g < steps; ++g)
    {
        bool ev[kRows];
        stepEvents (p, g, o, ev);
        emit (g, c.stepTime (g, o.swing), ev);
    }
}
} // namespace r78
