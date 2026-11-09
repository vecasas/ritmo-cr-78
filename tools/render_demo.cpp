// Renderiza cada voz y un ritmo de ejemplo a WAV para escucharlos sin DAW.
// Uso: render_demo <carpeta_salida>
#include "../src/dsp/Voices.h"
#include "../src/seq/Sequencer.h"
#include <cstdio>
#include <string>
#include <vector>

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::perror (path.c_str()); return; }
    const uint32_t n = (uint32_t) l.size();
    const uint32_t dataBytes = n * 2 * 2;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (1); w16 (2); w32 ((uint32_t) sr);
    w32 ((uint32_t) sr * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        int16_t a = (int16_t) std::lround (std::clamp (l[i], -1.0f, 1.0f) * 32767.0f);
        int16_t b = (int16_t) std::lround (std::clamp (r[i], -1.0f, 1.0f) * 32767.0f);
        std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
    }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    const int sr = 48000;

    // 1) Cada instrumento: 4 golpes (normal, acento, normal con decay largo, tono alto).
    std::vector<float> L, R;
    {
        r78::DrumKit kit;
        kit.setSampleRate ((float) sr);
        for (int i = 0; i < r78::kNumInst; ++i)
        {
            auto& v = kit.voice (i);
            const int hits = 3;
            for (int h = 0; h < hits; ++h)
            {
                v.p = {};
                kit.trigger (i, 1.0f, h == 1);
                const int len = (int) (sr * (i == (int) r78::Inst::Cymbal ? 1.0f : 0.35f));
                for (int s = 0; s < len; ++s) { float l, r; kit.process (l, r); L.push_back (l * 0.7f); R.push_back (r * 0.7f); }
            }
            for (int s = 0; s < sr / 4; ++s) { float l, r; kit.process (l, r); L.push_back (l * 0.7f); R.push_back (r * 0.7f); }
        }
        writeWav (out + "/01_instrumentos.wav", L, R, sr);
    }

    // 2) Ritmo de ejemplo (patrón propio de 16 pasos, 112 BPM, 4 compases).
    {
        r78::DrumKit kit;
        kit.setSampleRate ((float) sr);
        using I = r78::Inst;
        struct Line { I inst; const char* steps; float pan; };
        const Line lines[] = {
            { I::Kick,      "x.....x...x.....", 0.0f },
            { I::Snare,     "....x.......x...", 0.0f },
            { I::HiHat,     "x.x.x.x.x.x.x.xx", 0.3f },
            { I::LoConga,   "..x....x..x..x..", -0.4f },
            { I::HiBongo,   ".x...x.....x..x.", -0.2f },
            { I::Maracas,   "xxxxxxxxxxxxxxxx", 0.5f },
            { I::Claves,    "..x..x....x.....", 0.4f },
            { I::Cowbell,   "...............x", 0.2f },
        };
        const char* accents = "x...x...x...x...";
        for (auto& ln : lines) kit.voice ((int) ln.inst).p.pan = ln.pan;
        kit.voice ((int) I::Maracas).p.level = 0.45f;
        kit.voice ((int) I::Claves).p.level = 0.55f;

        const float bpm = 112.0f;
        const int stepLen = (int) (sr * 60.0f / bpm / 4.0f);
        std::vector<float> l2, r2;
        for (int bar = 0; bar < 4; ++bar)
            for (int st = 0; st < 16; ++st)
            {
                for (auto& ln : lines)
                    if (ln.steps[st] == 'x')
                        kit.trigger ((int) ln.inst, 1.0f, accents[st] == 'x');
                if (bar == 3 && st == 0) kit.trigger ((int) I::Cymbal, 1.0f, true);
                for (int s = 0; s < stepLen; ++s) { float l, r; kit.process (l, r); l2.push_back (l * 0.5f); r2.push_back (r * 0.5f); }
            }
        for (int s = 0; s < sr; ++s) { float l, r; kit.process (l, r); l2.push_back (l * 0.5f); r2.push_back (r * 0.5f); }
        writeWav (out + "/02_ritmo_demo.wav", l2, r2, sr);
    }
    // 3) Catálogo de ritmos de fábrica: A, A, B y Fill de cada uno, con el
    //    mismo motor de pasos que usa el plugin.
    {
        std::vector<float> l3, r3;
        for (auto& d : r78::factoryRhythms())
        {
            r78::DrumKit kit;
            kit.setSampleRate ((float) sr);
            r78::Pattern pat;
            r78::loadRhythm (pat, d);
            r78::StepClock clock;
            clock.locate (0.0, d.stepsPerBeat);
            const double beatsPerBar = (double) d.length / d.stepsPerBeat;
            const double ppqPerSample = d.bpm / 60.0 / sr;
            const int vars[4] { r78::VarA, r78::VarA, r78::VarB, r78::VarFill };
            double ppq = 0.0;
            while (ppq < beatsPerBar * 4.0)
            {
                for (long g; (g = clock.poll (ppq, 0.5f)) >= 0;)
                {
                    const int step = (int) (g % d.length);
                    const int var = vars[std::min<long> (3, g / d.length)];
                    const bool acc = pat.get (var, r78::kAccentRow, step);
                    for (int i = 0; i < r78::kNumInst; ++i)
                        if (pat.get (var, i, step)) kit.trigger (i, acc ? 1.0f : 0.73f, true);
                }
                float l, r; kit.process (l, r);
                l3.push_back (l * 0.5f); r3.push_back (r * 0.5f);
                ppq += ppqPerSample;
            }
            for (int s = 0; s < sr / 2; ++s) { float l, r; kit.process (l, r); l3.push_back (l * 0.5f); r3.push_back (r * 0.5f); }
        }
        writeWav (out + "/04_ritmos_fabrica.wav", l3, r3, sr);
    }
    std::puts ("ok");
    // 5) Fade in, Cancelar voz y fade out sobre Synth Pop (16 compases):
    //    compases 1-4 fade in, 7-8 sin bombo, 9-10 sin charles/platillo, 13-16 fade out.
    {
        const auto& list = r78::factoryRhythms();
        const r78::RhythmDef* d = &list[0];
        for (auto& r : list) if (std::string (r.name) == "Synth Pop") d = &r;
        r78::DrumKit kit;
        kit.setSampleRate ((float) sr);
        r78::Pattern pat;
        r78::loadRhythm (pat, *d);
        r78::StepClock clock;
        clock.locate (0.0, d->stepsPerBeat);
        r78::PlayOptions o;
        const double beatsPerBar = (double) d->length / d->stepsPerBeat;
        const double secPerBar = beatsPerBar * 60.0 / d->bpm;
        const double ppqPerSample = d->bpm / 60.0 / sr;
        r78::Fader fader;
        fader.start (+1, (float) (4 * secPerBar), sr, true);
        bool fadingOut = false, stopped = false;
        std::vector<float> l5, r5;
        double ppq = 0.0;
        while (! stopped && ppq < beatsPerBar * 17.0)
        {
            const long bar = (long) (ppq / beatsPerBar);
            if (bar >= 12 && ! fadingOut) { fader.start (-1, (float) (4 * secPerBar), sr, false); fadingOut = true; }
            bool cancel[r78::kCancelGroups] {};
            cancel[0] = bar == 6 || bar == 7;
            cancel[2] = bar == 8 || bar == 9;
            for (long g; (g = clock.poll (ppq, 0.5f)) >= 0;)
            {
                bool ev[r78::kRows];
                r78::stepEvents (pat, g, o, ev);
                const float vel = ev[r78::kAccentRow] ? 1.0f : 0.73f;
                for (int i = 0; i < r78::kNumInst; ++i)
                    if (ev[i] && ! cancel[r78::kCancelGroupOf[i]]) kit.trigger (i, vel, true);
            }
            float l, r; kit.process (l, r);
            stopped = fader.tick();
            l5.push_back (l * 0.5f * fader.gain); r5.push_back (r * 0.5f * fader.gain);
            ppq += ppqPerSample;
        }
        writeWav (out + "/09_fade_y_cancelar.wav", l5, r5, sr);
    }

    return 0;
}
