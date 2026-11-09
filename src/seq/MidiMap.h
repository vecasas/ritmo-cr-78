#pragma once
#include "../dsp/Voices.h"
#include <array>
#include <atomic>

namespace r78
{
// Mapa nota MIDI -> instrumento, editable por "aprender". Sin JUCE para poder probarlo.
struct MidiMap
{
    std::array<std::atomic<int>, kNumInst> note {};

    MidiMap() { reset(); }
    void reset() { for (int i = 0; i < kNumInst; ++i) note[(size_t) i].store (kInstInfo[(size_t) i].midiNote); }

    int instFor (int n) const
    {
        for (int i = 0; i < kNumInst; ++i) if (note[(size_t) i].load() == n) return i;
        return -1;
    }
    // Asigna la nota n al instrumento inst. Si otro instrumento la tenía, se
    // intercambian las notas para que no queden dos en la misma.
    void assign (int inst, int n)
    {
        if (inst < 0 || inst >= kNumInst || n < 0 || n > 127) return;
        const int other = instFor (n);
        if (other >= 0 && other != inst) note[(size_t) other].store (note[(size_t) inst].load());
        note[(size_t) inst].store (n);
    }
};

// Cómo se interpreta la velocidad de una nota entrante.
enum MidiVelMode { VelDynamic = 0, VelTwoLevels = 1, VelFixed = 2 };

// Calcula nivel y acento de un golpe MIDI.
//  - Dinámica: el nivel sigue a la velocidad; acento si supera el umbral.
//  - Dos niveles: como el secuenciador, normal o acentuado según el umbral.
//  - Fija: siempre el mismo nivel; el acento solo llega por la nota de acento.
inline void midiHit (int velocity, int mode, int threshold, bool accentNote, float& level, bool& accent)
{
    const bool overThr = velocity >= threshold;
    switch (mode)
    {
        case VelTwoLevels: accent = overThr || accentNote; level = 1.0f; break;
        case VelFixed:     accent = accentNote;            level = 1.0f; break;
        default:           accent = overThr || accentNote; level = (float) velocity / 127.0f; break;
    }
}
} // namespace r78
