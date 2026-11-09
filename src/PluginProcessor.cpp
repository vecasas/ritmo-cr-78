#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::String RitmoProcessor::pid (int inst, const char* param)
{
    return juce::String (r78::kInstInfo[(size_t) inst].id) + "_" + param;
}

RitmoProcessor::RitmoProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
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
        p.send  = apvts.getRawParameterValue (pid (i, "send"));
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

// Cambiar de ritmo en el selector carga el patrón de fábrica (en el hilo de mensajes).
void RitmoProcessor::timerCallback()
{
    const int sel = (int) apvts.getRawParameterValue ("rhythm")->load();
    if (sel != loadedRhythm) loadFactoryRhythm (sel);
}

juce::AudioProcessorValueTreeState::ParameterLayout RitmoProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", 0.0f, 1.0f, 0.8f));

    // Secuenciador
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "play", 1 }, "Play", false));
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
        layout.add (std::move (grp));
    }
    return layout;
}

bool RitmoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
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
    }
}

void RitmoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    updateVoiceParams();
    updateFxParams();

    for (int i = 0; i < r78::kNumInst; ++i)
        if (const int v = pendingPad[(size_t) i].exchange (0))
            kit.trigger (i, 1.0f, v == 2);

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

    const int spb = juce::jlimit (1, 8, pattern.stepsPerBeat.load());
    if (running)
    {
        if (! wasRunning && ! hostDriven) ppq = 0.0;
        if (! wasRunning || spb != clockSpb || (hostDriven && std::abs (ppq - expectedPpq) > 0.01))
        {
            clock.locate (ppq, spb);
            clockSpb = spb;
        }
    }
    else if (wasRunning)
    {
        currentStep.store (-1);
    }
    wasRunning = running;
    const double ppqPerSample = bpm / 60.0 / sampleRate;

    const float gain = master->load();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    auto midiIt = midi.cbegin();
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        for (; midiIt != midi.cend() && (*midiIt).samplePosition <= n; ++midiIt)
        {
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())
            {
                const int inst = kit.instForNote (msg.getNoteNumber());
                // Velocidad > 100 se interpreta como acento.
                kit.trigger (inst, msg.getFloatVelocity(), msg.getVelocity() > 100);
            }
        }
        if (running)
        {
            for (long g; (g = clock.poll (ppq, opts.swing)) >= 0;)
                fireStep (g);
            ppq += ppqPerSample;
        }

        float l, r, sl, sr;
        kit.process (l, r, sl, sr);
        fx.process (sl, sr, bpm);
        l += sl; r += sr;
        masterBus.process (l, r);
        L[n] = l * gain;
        R[n] = r * gain;
    }
    internalPpq = ppq;
    expectedPpq = ppq;
    compGainReduction.store (masterBus.comp.gainReductionDb);
}

void RitmoProcessor::fireStep (long g)
{
    bool ev[r78::kRows];
    const int var = r78::stepEvents (pattern, g, opts, ev);
    const int len = juce::jlimit (1, r78::kMaxSteps, pattern.length.load());
    // Con acento suena a nivel completo; sin acento baja según el control "Acento".
    const float vel = ev[r78::kAccentRow] ? 1.0f : 1.0f - 0.45f * accentAmount;
    for (int i = 0; i < r78::kNumInst; ++i)
        if (ev[i])
            kit.trigger (i, vel, true);
    currentStep.store ((int) (g % len));
    currentVar.store (var);
}

juce::AudioProcessorEditor* RitmoProcessor::createEditor() { return new RitmoEditor (*this); }

void RitmoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("pattern", patternToString(), nullptr);
    state.setProperty ("loadedRhythm", loadedRhythm, nullptr);
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
            if (state.hasProperty ("pattern")) patternFromString (state["pattern"].toString());
        }
}

// Serializa el patrón: "longitud,pasosPorPulso|var:fila:pasos|..."
juce::String RitmoProcessor::patternToString() const
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

void RitmoProcessor::patternFromString (const juce::String& str)
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
