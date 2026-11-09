#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::String RitmoProcessor::pid (int inst, const char* param)
{
    return juce::String (r78::kInstInfo[(size_t) inst].id) + "_" + param;
}

RitmoProcessor::RitmoProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < r78::kNumInst; ++i)
    {
        auto& p = vp[(size_t) i];
        p.level = apvts.getRawParameterValue (pid (i, "level"));
        p.tune  = apvts.getRawParameterValue (pid (i, "tune"));
        p.decay = apvts.getRawParameterValue (pid (i, "decay"));
        p.tone  = apvts.getRawParameterValue (pid (i, "tone"));
        p.pan   = apvts.getRawParameterValue (pid (i, "pan"));
        p.mute  = apvts.getRawParameterValue (pid (i, "mute"));
        if (i < r78::kCancelGroups) cancelParams[(size_t) i] = apvts.getRawParameterValue ("cancel_" + juce::String (i));
        p.send  = apvts.getRawParameterValue (pid (i, "send"));
        p.out   = apvts.getRawParameterValue (pid (i, "out"));
    }
    master = apvts.getRawParameterValue ("master");
    loadFactoryRhythm (0);
    startTimerHz (20);
}

juce::StringArray RitmoProcessor::rhythmNames()
{
    juce::StringArray names;
    for (auto& r : r78::factoryRhythms()) names.add (juce::String (juce::CharPointer_UTF8 (r.name)));
    return names;
}

void RitmoProcessor::loadFactoryRhythm (int index)
{
    const auto& list = r78::factoryRhythms();
    if (index < 0 || index >= (int) list.size()) return;
    r78::loadRhythm (pattern, list[(size_t) index]);
    loadedRhythm = index;
}

r78::Pattern& RitmoProcessor::editPattern()
{
    switch (mode())
    {
        case 1: return userPatterns[(size_t) userSlot()];
        case 2:
        {
            const int pos = juce::jlimit (0, kSongSteps - 1, songEditPos.load());
            const int idx = pos < songLength.load() ? song[(size_t) pos].load() : -1;
            return userPatterns[(size_t) (idx >= 0 ? idx : 0)];
        }
        default: return pattern;
    }
}

bool RitmoProcessor::patternHasNotes (const r78::Pattern& p) const
{
    for (int v = 0; v < r78::kNumVars; ++v)
        for (int r = 0; r < r78::kNumInst; ++r)
            for (int st = 0; st < r78::kMaxSteps; ++st)
                if (p.get (v, r, st)) return true;
    return false;
}

// Cambiar de ritmo en el selector carga el patrón de fábrica (en el hilo de mensajes).
void RitmoProcessor::requestFade (int dir)
{
    if (dir > 0)
        if (auto* p = apvts.getParameter ("play"); p != nullptr && p->getValue() < 0.5f)
        {
            fadeRequest.store (dir);   // antes de arrancar, para empezar en silencio
            p->setValueNotifyingHost (1.0f);
            return;
        }
    fadeRequest.store (dir);
}

void RitmoProcessor::timerCallback()
{
    const int sel = (int) apvts.getRawParameterValue ("rhythm")->load();
    if (sel != loadedRhythm) loadFactoryRhythm (sel);
    if (stopRequested.exchange (false))
        if (auto* p = apvts.getParameter ("play")) p->setValueNotifyingHost (0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout RitmoProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", 0.0f, 1.0f, 0.8f));

    // Secuenciador
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "play", 1 }, "Play", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Modo",
                                                        StringArray { String (CharPointer_UTF8 ("Fábrica")), "Usuario", String (CharPointer_UTF8 ("Canción")) }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "user_slot", 1 }, "Patrón de usuario", 1, kUserSlots, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rhythm", 1 }, "Ritmo", rhythmNames(), 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Tempo",
                                                       NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing",
                                                       NormalisableRange<float> (50.0f, 75.0f, 0.1f), 50.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "variation", 1 }, "Variación",
                                                        StringArray { "A", "B", "A+B" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fill", 1 }, "Fill cada",
                                                        StringArray { "Nunca", "2", "4", "8", "16" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fill_type", 1 }, "Tipo de fill",
                                                        StringArray { "Fill del ritmo", "A/B", "Break", "Redoble bongo", "Redoble caja" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "accent", 1 }, "Acento", 0.0f, 1.0f, 0.6f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Sincronizar con el DAW", true));
    // MIDI
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "midi_in_ch", 1 }, "Canal MIDI de entrada", 0, 16, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? String ("Omni") : String (v); })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "midi_vel", 1 }, "Velocidad MIDI",
        StringArray { String (CharPointer_UTF8 ("Dinámica")), "Dos niveles", "Fija" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "midi_acc_thr", 1 }, "Umbral de acento", 1, 127, 100));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "midi_acc_note", 1 }, "Nota de acento", -1, 127, -1,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) {
            return v < 0 ? String ("Ninguna") : MidiMessage::getMidiNoteName (v, true, true, 3) + " (" + String (v) + ")"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "midi_out", 1 }, "Enviar el ritmo por MIDI", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "midi_out_ch", 1 }, "Canal MIDI de salida", 1, 16, 10));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fade_time", 1 }, "Tiempo de fade",
                                                       NormalisableRange<float> (1.0f, 30.0f, 0.1f, 0.5f), 8.0f));
    {
        static const char* cancelNames[r78::kCancelGroups] { "Cancelar bombo", "Cancelar caja y aro", "Cancelar charles, platillo y metal",
                                                             "Cancelar bongos y conga", "Cancelar claves y cencerro",
                                                             "Cancelar maracas, guiro y pandereta" };
        for (int c = 0; c < r78::kCancelGroups; ++c)
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { "cancel_" + String (c), 1 }, cancelNames[c], false));
    }

    // Efectos (cadena de envío)
    auto pct = [] (const char* id, const char* nm, float def) {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, 0.0f, 1.0f, def);
    };
    auto onOff = [] (const char* id, const char* nm, bool def) {
        return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, nm, def);
    };
    layout.add (onOff ("od_on", "Overdrive On", false), pct ("od_drive", "Overdrive Drive", 0.4f),
                pct ("od_tone", "Overdrive Tono", 0.5f), pct ("od_mix", "Overdrive Mezcla", 1.0f));
    layout.add (onOff ("fl_on", "Flanger On", false), pct ("fl_rate", "Flanger Velocidad", 0.3f),
                pct ("fl_depth", "Flanger Profundidad", 0.6f), pct ("fl_fb", "Flanger Realimentación", 0.4f),
                pct ("fl_mix", "Flanger Mezcla", 0.5f));
    layout.add (onOff ("dl_on", "Delay On", false),
                std::make_unique<AudioParameterChoice> (ParameterID { "dl_time", 1 }, "Delay Tiempo",
                    StringArray { "1/16", "1/8", "1/8 tresillo", "1/8 con puntillo", "1/4", "1/4 con puntillo", "1/2" }, 3),
                pct ("dl_fb", "Delay Realimentación", 0.35f), pct ("dl_damp", "Delay Amortiguación", 0.4f),
                pct ("dl_mix", "Delay Mezcla", 0.3f), onOff ("dl_pp", "Delay Ping-pong", true));
    layout.add (onOff ("rv_on", "Reverb On", true), pct ("rv_size", "Reverb Tamaño", 0.7f),
                std::make_unique<AudioParameterFloat> (ParameterID { "rv_gate", 1 }, "Reverb Gate (ms)",
                                                       NormalisableRange<float> (40.0f, 800.0f, 1.0f, 0.5f), 250.0f),
                onOff ("rv_gate_on", "Reverb Gate On", true), pct ("rv_mix", "Reverb Mezcla", 0.35f));

    // Máster: compresor y EQ
    layout.add (onOff ("comp_on", "Compresor On", true),
                std::make_unique<AudioParameterFloat> (ParameterID { "comp_thr", 1 }, "Compresor Umbral",
                                                       NormalisableRange<float> (-40.0f, 0.0f, 0.1f), -14.0f),
                std::make_unique<AudioParameterFloat> (ParameterID { "comp_ratio", 1 }, "Compresor Ratio",
                                                       NormalisableRange<float> (1.0f, 20.0f, 0.1f, 0.5f), 3.0f),
                std::make_unique<AudioParameterFloat> (ParameterID { "comp_att", 1 }, "Compresor Ataque (ms)",
                                                       NormalisableRange<float> (0.1f, 100.0f, 0.1f, 0.4f), 8.0f),
                std::make_unique<AudioParameterFloat> (ParameterID { "comp_rel", 1 }, "Compresor Relajación (ms)",
                                                       NormalisableRange<float> (10.0f, 1000.0f, 1.0f, 0.4f), 120.0f),
                std::make_unique<AudioParameterFloat> (ParameterID { "comp_makeup", 1 }, "Compresor Ganancia",
                                                       NormalisableRange<float> (0.0f, 24.0f, 0.1f), 3.0f));
    layout.add (onOff ("eq_on", "EQ On", true));
    static const char* eqNames[] { "EQ 60 Hz", "EQ 150 Hz", "EQ 400 Hz", "EQ 1 kHz", "EQ 3 kHz", "EQ 8 kHz" };
    for (int b = 0; b < 6; ++b)
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "eq_" + String (b), 1 }, eqNames[b],
                                                           NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

    for (int i = 0; i < r78::kNumInst; ++i)
    {
        const String name (CharPointer_UTF8 (r78::kInstInfo[(size_t) i].name));
        auto grp = std::make_unique<AudioProcessorParameterGroup> (r78::kInstInfo[(size_t) i].id, name, " | ");
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid (i, "level"), 1 }, name + " Nivel", 0.0f, 1.0f, 0.8f));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid (i, "tune"), 1 }, name + " Afinación",
                                                              NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid (i, "decay"), 1 }, name + " Decay", 0.0f, 1.0f, 0.5f));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid (i, "tone"), 1 }, name + " Tono", 0.0f, 1.0f, 0.5f));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid (i, "pan"), 1 }, name + " Pan", -1.0f, 1.0f, 0.0f));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "mute"), 1 }, name + " Mute", false));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "send"), 1 }, name + " Envío FX", false));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "out"), 1 }, name + " Salida",
                                                               StringArray { "Principal", "Salida 2", "Salida 3", "Salida 4",
                                                                             "Salida 5", "Salida 6", "Salida 7", "Salida 8" }, 0));
        layout.add (std::move (grp));
    }
    return layout;
}

// Salida principal estéreo y 7 salidas estéreo extra (desactivadas por defecto).
juce::AudioProcessor::BusesProperties RitmoProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Principal", juce::AudioChannelSet::stereo(), true);
    for (int k = 2; k <= 1 + kNumAux; ++k)
        b = b.withOutput ("Salida " + juce::String (k), juce::AudioChannelSet::stereo(), false);
    return b;
}

bool RitmoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int k = 1; k < layouts.outputBuses.size(); ++k)
    {
        const auto& set = layouts.outputBuses.getReference (k);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void RitmoProcessor::prepareToPlay (double sampleRate, int)
{
    kit.setSampleRate ((float) sampleRate);
    fx.prepare ((float) sampleRate);
    masterBus.prepare ((float) sampleRate);
}

void RitmoProcessor::updateFxParams()
{
    fx.odOn = p ("od_on") > 0.5f; fx.od.drive = p ("od_drive"); fx.od.tone = p ("od_tone"); fx.od.mix = p ("od_mix");
    fx.flOn = p ("fl_on") > 0.5f; fx.fl.rate = p ("fl_rate"); fx.fl.depth = p ("fl_depth");
    fx.fl.feedback = p ("fl_fb"); fx.fl.mix = p ("fl_mix");
    static constexpr float beats[] { 0.25f, 0.5f, 1.0f / 3.0f, 0.75f, 1.0f, 1.5f, 2.0f };
    fx.dlOn = p ("dl_on") > 0.5f; fx.dl.timeBeats = beats[juce::jlimit (0, 6, (int) p ("dl_time"))];
    fx.dl.feedback = p ("dl_fb"); fx.dl.damp = p ("dl_damp"); fx.dl.mix = p ("dl_mix"); fx.dl.pingPong = p ("dl_pp") > 0.5f;
    fx.rvOn = p ("rv_on") > 0.5f; fx.rv.size = p ("rv_size"); fx.rv.gateMs = p ("rv_gate");
    fx.rv.gateOn = p ("rv_gate_on") > 0.5f; fx.rv.mix = p ("rv_mix");

    auto& c = masterBus.comp;
    masterBus.compOn = p ("comp_on") > 0.5f;
    c.thresholdDb = p ("comp_thr"); c.ratio = p ("comp_ratio"); c.attackMs = p ("comp_att");
    c.releaseMs = p ("comp_rel"); c.makeupDb = p ("comp_makeup");
    masterBus.eqOn = p ("eq_on") > 0.5f;
    static const char* eqIds[] { "eq_0", "eq_1", "eq_2", "eq_3", "eq_4", "eq_5" };
    for (int b = 0; b < 6; ++b) masterBus.eq.gainsDb[(size_t) b] = p (eqIds[b]);
}

void RitmoProcessor::updateVoiceParams()
{
    for (int i = 0; i < r78::kNumInst; ++i)
    {
        auto& p = kit.voice (i).p;
        const auto& s = vp[(size_t) i];
        p.level = s.mute->load() > 0.5f ? 0.0f : s.level->load();
        p.tune = s.tune->load();
        p.decay = s.decay->load();
        p.tone = s.tone->load();
        p.pan = s.pan->load();
        p.send = s.send->load() > 0.5f;
        kit.outBus[(size_t) i] = (int) s.out->load();
    }
}

void RitmoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    updateVoiceParams();
    updateFxParams();

    midiOutOn = p ("midi_out") > 0.5f;
    midiOutCh = (int) p ("midi_out_ch");
    std::array<int, r78::kNumInst> padHits {};
    for (int i = 0; i < r78::kNumInst; ++i)
        padHits[(size_t) i] = pendingPad[(size_t) i].exchange (0);

    // ---- Secuenciador: decidir reloj (DAW o interno) ----
    const double sampleRate = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    double bpm = apvts.getRawParameterValue ("tempo")->load();
    bool running = apvts.getRawParameterValue ("play")->load() > 0.5f;
    double ppq = internalPpq;
    bool hostDriven = false;

    if (apvts.getRawParameterValue ("sync")->load() > 0.5f && wrapperType != wrapperType_Standalone)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (pos->getIsPlaying())
                {
                    hostDriven = true;
                    running = true;
                    if (auto b = pos->getBpm()) bpm = *b;
                    if (auto q = pos->getPpqPosition()) ppq = *q;
                }

    opts.swing = apvts.getRawParameterValue ("swing")->load() / 100.0f;
    opts.variation = (int) apvts.getRawParameterValue ("variation")->load();
    {
        static constexpr int fills[] { 0, 2, 4, 8, 16 };
        opts.fillEvery = fills[juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("fill")->load())];
    }
    opts.fillType = (int) apvts.getRawParameterValue ("fill_type")->load();
    accentAmount = apvts.getRawParameterValue ("accent")->load();

    const int curMode = mode();
    const r78::Pattern& refPat = curMode == 1 ? userPatterns[(size_t) userSlot()] : pattern;
    const int spb = juce::jlimit (1, 8, curMode == 2 ? 4 : refPat.stepsPerBeat.load());
    // Fade: atender peticiones de la interfaz; al arrancar o parar se vuelve a volumen normal.
    // Un fade in pedido con la caja parada espera a que arranque; un fade out con la caja parada se descarta.
    if (running != wasRunning) fader.reset();
    if (const int req = fadeRequest.load(); req != 0 && (running || req < 0))
    {
        fadeRequest.store (0);
        if (running)
            fader.start (req, apvts.getRawParameterValue ("fade_time")->load(), sampleRate, req > 0 && ! wasRunning);
    }
    if (running)
    {
        if (! wasRunning && ! hostDriven) ppq = 0.0;
        if (! wasRunning || spb != clockSpb || (hostDriven && std::abs (ppq - expectedPpq) > 0.01))
        {
            clock.locate (ppq, spb);
            clockSpb = spb;
            songCursor.reset();   // la canción empieza desde el principio
        }
    }
    else if (wasRunning)
    {
        currentStep.store (-1);
        songPlayPos.store (-1);
        playingPattern.store (nullptr);
    }
    wasRunning = running;
    for (int i = 0; i < r78::kNumInst; ++i)
        if (padHits[(size_t) i])
        {
            kit.trigger (i, 1.0f, padHits[(size_t) i] == 2);
            blockPos = 0;
            sendMidiHit (i, padHits[(size_t) i] == 2, 0);
            if (running && curMode != 0) recordPad (i, ppq, opts.swing);   // grabación en tiempo real
        }
    const double ppqPerSample = bpm / 60.0 / sampleRate;

    const float gain = master->load();
    auto mainBus = getBusBuffer (buffer, false, 0);
    auto* L = mainBus.getWritePointer (0);
    auto* R = mainBus.getWritePointer (1);
    // Salidas extra activas en el host. Un instrumento enviado a una salida
    // desactivada vuelve a la principal (con efectos y máster).
    std::array<std::array<float*, 2>, kNumAux> auxPtr {};
    std::array<float, kNumAux> auxL {}, auxR {};
    std::array<bool, kNumAux> auxOn {};
    int numAux = 0;
    for (int k = 0; k < kNumAux && k + 1 < getBusCount (false); ++k)
    {
        auto* bus = getBus (false, k + 1);
        if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() < 2) continue;
        auto bb = getBusBuffer (buffer, false, k + 1);
        auxPtr[(size_t) k] = { bb.getWritePointer (0), bb.getWritePointer (1) };
        auxOn[(size_t) k] = true;
        numAux = k + 1;
    }
    for (auto& ob : kit.outBus)
        if (ob > 0 && (ob > kNumAux || ! auxOn[(size_t) (ob - 1)])) ob = 0;

    const int inCh = (int) p ("midi_in_ch");
    const int velMode = (int) p ("midi_vel");
    const int accThr = (int) p ("midi_acc_thr");
    const int accNote = (int) p ("midi_acc_note");
    if (accNote < 0) accentHeld = false;

    auto midiIt = midi.cbegin();
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        for (; midiIt != midi.cend() && (*midiIt).samplePosition <= n; ++midiIt)
        {
            const auto msg = (*midiIt).getMessage();
            if (inCh != 0 && msg.getChannel() != inCh) continue;
            const int note = msg.getNoteNumber();
            if (msg.isNoteOff() && note == accNote) { accentHeld = false; continue; }
            if (! msg.isNoteOn()) continue;
            lastMidiNote.store (note);
            if (const int li = learnInst.load(); li >= 0)            // aprender: la nota pasa a este instrumento
            {
                midiMap.assign (li, note);
                learnInst.store (-1);
            }
            if (note == accNote) { accentHeld = true; continue; }    // la nota de acento no suena
            const int inst = midiMap.instFor (note);
            if (inst < 0) continue;
            // También acentúa si la nota de acento llega en la misma muestra (acorde).
            bool accentNow = accentHeld;
            for (const auto m2 : midi)
                if (m2.samplePosition == (*midiIt).samplePosition && m2.getMessage().isNoteOn()
                    && m2.getMessage().getNoteNumber() == accNote) accentNow = true;
            float lv; bool ac;
            r78::midiHit (msg.getVelocity(), velMode, accThr, accentNow, lv, ac);
            kit.trigger (inst, lv, ac);
        }
        blockPos = n;
        // Note-off de la salida MIDI.
        for (int i = 0; i < r78::kNumInst; ++i)
            if (noteOffIn[(size_t) i] > 0 && --noteOffIn[(size_t) i] == 0)
                midiOut.addEvent (juce::MidiMessage::noteOff (midiOutCh, noteOutNum[(size_t) i]), n);
        if (running)
        {
            for (long g; (g = clock.poll (ppq, opts.swing)) >= 0;)
                fireStep (g);
            ppq += ppqPerSample;
        }

        float l, r, sl, sr;
        kit.process (l, r, sl, sr, auxL.data(), auxR.data(), numAux);
        fx.process (sl, sr, bpm);
        l += sl; r += sr;
        masterBus.process (l, r);
        if (fader.tick()) stopRequested.store (true);   // fin del fade out: parar
        const float g = gain * fader.gain;
        L[n] = l * g;
        R[n] = r * g;
        for (int k = 0; k < numAux; ++k)   // salidas extra: en seco, con master y fade
        {
            if (! auxOn[(size_t) k]) continue;
            auxPtr[(size_t) k][0][n] = auxL[(size_t) k] * g;
            auxPtr[(size_t) k][1][n] = auxR[(size_t) k] * g;
        }
    }
    internalPpq = ppq;
    expectedPpq = ppq;
    // La salida MIDI sustituye a la entrada (no se reenvía lo recibido).
    midi.swapWith (midiOut);
    midiOut.clear();
    compGainReduction.store (masterBus.comp.gainReductionDb);
    fadeDirection.store (fader.dir);
}

// Devuelve el patrón que suena en el paso global g y el paso relativo a ese patrón.
const r78::Pattern* RitmoProcessor::patternForStep (long g, long& localStep, bool& songMode)
{
    const int m = mode();
    songMode = m == 2;
    if (m == 0) { localStep = g; return &pattern; }
    if (m == 1) { localStep = g; return &userPatterns[(size_t) userSlot()]; }

    const int e = r78::songStep (songCursor, g, songLength.load(),
                                 [this] (int i) { return song[(size_t) i].load(); },
                                 [this] (int idx) { return userPatterns[(size_t) juce::jlimit (0, kUserSlots - 1, idx)].length.load(); },
                                 localStep);
    if (e == kSongEnd) { stopRequested.store (true); return nullptr; }
    if (e < 0) return nullptr;
    songPlayPos.store (songCursor.pos);
    return &userPatterns[(size_t) juce::jlimit (0, kUserSlots - 1, e)];
}

void RitmoProcessor::fireStep (long g)
{
    long local = 0;
    bool songMode = false;
    const auto* pat = patternForStep (g, local, songMode);
    if (pat == nullptr) return;
    bool ev[r78::kRows];
    auto o = opts;
    if (songMode) { o.fillEvery = 0; }                    // en modo canción no hay fills
    const int var = r78::stepEvents (*pat, local, o, ev);
    const int len = juce::jlimit (1, r78::kMaxSteps, pat->length.load());
    // Con acento suena a nivel completo; sin acento baja según el control "Acento".
    const float vel = ev[r78::kAccentRow] ? 1.0f : 1.0f - 0.45f * accentAmount;
    for (int i = 0; i < r78::kNumInst; ++i)
        if (ev[i] && cancelParams[(size_t) r78::kCancelGroupOf[i]]->load() < 0.5f)   // Cancelar voz
        {
            kit.trigger (i, vel, true);
            sendMidiHit (i, ev[r78::kAccentRow], blockPos);
        }
    currentStep.store ((int) (local % len));
    currentVar.store (var);
    playingPattern.store (pat);
}

// Envía un golpe por la salida MIDI (acento = velocidad 127, normal = 90) con un note-off a los 30 ms.
void RitmoProcessor::sendMidiHit (int inst, bool accent, int samplePos)
{
    if (! midiOutOn) return;
    const int note = midiMap.note[(size_t) inst].load();
    if (note < 0 || note > 127) return;
    if (noteOffIn[(size_t) inst] > 0)
        midiOut.addEvent (juce::MidiMessage::noteOff (midiOutCh, noteOutNum[(size_t) inst]), samplePos);
    midiOut.addEvent (juce::MidiMessage::noteOn (midiOutCh, note, (juce::uint8) (accent ? 127 : 90)), samplePos);
    noteOutNum[(size_t) inst] = note;
    noteOffIn[(size_t) inst] = juce::jmax (1, (int) (getSampleRate() * 0.03));
}

juce::File RitmoProcessor::exportMidiFile (int bars)
{
    constexpr int tpq = 960;
    juce::MidiMessageSequence seq;
    const int ch = (int) p ("midi_out_ch");
    const double bpm = p ("tempo");
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0.0);

    r78::PlayOptions o;
    o.swing = p ("swing") / 100.0f;
    o.variation = (int) p ("variation");
    static constexpr int fills[] { 0, 2, 4, 8, 16 };
    o.fillEvery = fills[juce::jlimit (0, 4, (int) p ("fill"))];
    o.fillType = (int) p ("fill_type");

    auto addHits = [&] (const bool (&ev)[r78::kRows], double ppq) {
        for (int i = 0; i < r78::kNumInst; ++i)
        {
            if (! ev[i] || cancelParams[(size_t) r78::kCancelGroupOf[i]]->load() > 0.5f) continue;
            const int note = midiMap.note[(size_t) i].load();
            if (note < 0) continue;
            const double t = ppq * tpq;
            seq.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) (ev[r78::kAccentRow] ? 127 : 90)), t);
            seq.addEvent (juce::MidiMessage::noteOff (ch, note), t + tpq / 8.0);
        }
    };

    juce::String name;
    if (mode() == 2)
    {
        // Una pasada de la canción, hasta Fin, Bucle o el final de la lista.
        o.fillEvery = 0;
        double startPpq = 0.0;
        name = "Cancion";
        for (int i = 0; i < songLength.load(); ++i)
        {
            const int e = song[(size_t) i].load();
            if (e < 0) break;
            const auto& pat = userPatterns[(size_t) juce::jlimit (0, kUserSlots - 1, e)];
            const int len = juce::jlimit (1, r78::kMaxSteps, pat.length.load());
            const int spb = juce::jlimit (1, 8, pat.stepsPerBeat.load());
            r78::renderSteps (pat, o, len, [&] (long, double ppq, const bool (&ev)[r78::kRows]) { addHits (ev, startPpq + ppq); });
            startPpq += (double) len / spb;
        }
    }
    else
    {
        const auto& pat = editPattern();
        const int len = juce::jlimit (1, r78::kMaxSteps, pat.length.load());
        r78::renderSteps (pat, o, (long) len * juce::jmax (1, bars), [&] (long, double ppq, const bool (&ev)[r78::kRows]) { addHits (ev, ppq); });
        name = mode() == 1 ? "Usuario " + juce::String (userSlot() + 1)
                           : juce::String (juce::CharPointer_UTF8 (r78::factoryRhythms()[(size_t) juce::jlimit (0, (int) r78::factoryRhythms().size() - 1, loadedRhythm)].name));
    }
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("RitmoCR78");
    dir.createDirectory();
    auto file = dir.getChildFile (juce::File::createLegalFileName ("Ritmo CR-78 - " + name + ".mid"));
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (os.openedOk()) mf.writeTo (os);
    return file;
}

// Graba un golpe de pad en el paso más cercano del patrón que está sonando.
void RitmoProcessor::recordPad (int inst, double ppq, float swing)
{
    const long next = clock.peekNext();
    const double tNext = clock.stepTime (next, swing), tPrev = clock.stepTime (next - 1, swing);
    const long target = (tNext - ppq) < (ppq - tPrev) ? next : next - 1;
    if (target < 0) return;
    auto* pat = const_cast<r78::Pattern*> (playingPattern.load());
    if (pat == nullptr) return;
    const int len = juce::jlimit (1, r78::kMaxSteps, pat->length.load());
    const long local = mode() == 2 ? target - songCursor.barStart : target;
    pat->set (recordVar.load(), inst, (int) (((local % len) + len) % len), true);
}

juce::AudioProcessorEditor* RitmoProcessor::createEditor() { return new RitmoEditor (*this); }

void RitmoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("pattern", patternToString (pattern), nullptr);
    // Patrones de usuario (solo los que tienen notas) y canción.
    auto user = state.getOrCreateChildWithName ("USER", nullptr);
    user.removeAllChildren (nullptr);
    for (int i = 0; i < kUserSlots; ++i)
        if (patternHasNotes (userPatterns[(size_t) i]))
        {
            juce::ValueTree slot ("SLOT");
            slot.setProperty ("index", i, nullptr);
            slot.setProperty ("data", patternToString (userPatterns[(size_t) i]), nullptr);
            user.appendChild (slot, nullptr);
        }
    juce::StringArray songItems;
    for (int i = 0; i < songLength.load(); ++i) songItems.add (juce::String (song[(size_t) i].load()));
    state.setProperty ("song", songItems.joinIntoString (","), nullptr);
    state.setProperty ("loadedRhythm", loadedRhythm, nullptr);
    juce::StringArray notes;
    for (auto& nn : midiMap.note) notes.add (juce::String (nn.load()));
    state.setProperty ("notemap", notes.joinIntoString (","), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void RitmoProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (state);
            loadedRhythm = state.getProperty ("loadedRhythm", (int) apvts.getRawParameterValue ("rhythm")->load());
            if (state.hasProperty ("pattern")) patternFromString (pattern, state["pattern"].toString());
            for (auto& up : userPatterns) { up.clear(); up.length.store (16); up.stepsPerBeat.store (4); }
            for (auto slot : state.getChildWithName ("USER"))
            {
                const int i = slot["index"];
                if (i >= 0 && i < kUserSlots) patternFromString (userPatterns[(size_t) i], slot["data"].toString());
            }
            const auto items = juce::StringArray::fromTokens (state["song"].toString(), ",", "");
            int n = 0;
            for (auto& it : items)
                if (it.isNotEmpty() && n < kSongSteps) song[(size_t) n++].store (it.getIntValue());
            songLength.store (n);
            midiMap.reset();
            const auto nm = juce::StringArray::fromTokens (state["notemap"].toString(), ",", "");
            if (nm.size() == r78::kNumInst)
                for (int i = 0; i < r78::kNumInst; ++i) midiMap.note[(size_t) i].store (juce::jlimit (-1, 127, nm[i].getIntValue()));
        }
}

// Serializa el patrón: "longitud,pasosPorPulso|var:fila:pasos|..."
juce::String RitmoProcessor::patternToString (const r78::Pattern& pattern)
{
    juce::String out;
    out << pattern.length.load() << "," << pattern.stepsPerBeat.load();
    for (int v = 0; v < r78::kNumVars; ++v)
        for (int r = 0; r < r78::kRows; ++r)
        {
            juce::String row;
            for (int s = 0; s < r78::kMaxSteps; ++s) row << (pattern.get (v, r, s) ? "x" : ".");
            out << "|" << v << ":" << r << ":" << row;
        }
    return out;
}

void RitmoProcessor::patternFromString (r78::Pattern& pattern, const juce::String& str)
{
    auto parts = juce::StringArray::fromTokens (str, "|", "");
    if (parts.isEmpty()) return;
    const auto head = juce::StringArray::fromTokens (parts[0], ",", "");
    pattern.clear();
    pattern.length.store (juce::jlimit (1, r78::kMaxSteps, head[0].getIntValue()));
    pattern.stepsPerBeat.store (juce::jlimit (1, 8, head[1].getIntValue()));
    for (int i = 1; i < parts.size(); ++i)
    {
        const auto f = juce::StringArray::fromTokens (parts[i], ":", "");
        if (f.size() != 3) continue;
        const int v = f[0].getIntValue(), r = f[1].getIntValue();
        if (v < 0 || v >= r78::kNumVars || r < 0 || r >= r78::kRows) continue;
        for (int s = 0; s < r78::kMaxSteps && s < f[2].length(); ++s)
            pattern.set (v, r, s, f[2][s] == 'x');
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RitmoProcessor(); }
