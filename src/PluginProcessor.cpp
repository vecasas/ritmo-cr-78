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
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "accent", 1 }, "Acento", 0.0f, 1.0f, 0.6f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Sincronizar con el DAW", true));

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
    }
}

void RitmoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    updateVoiceParams();

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

        float l, r;
        kit.process (l, r);
        L[n] = l * gain;
        R[n] = r * gain;
    }
    internalPpq = ppq;
    expectedPpq = ppq;
}

void RitmoProcessor::fireStep (long g)
{
    const int len = juce::jlimit (1, r78::kMaxSteps, pattern.length.load());
    const int step = (int) (g % len);
    const long bar = g / len;
    const int var = r78::variationForBar (bar, opts);
    const bool acc = pattern.get (var, r78::kAccentRow, step);
    // Con acento suena a nivel completo; sin acento baja según el control "Acento".
    const float vel = acc ? 1.0f : 1.0f - 0.45f * accentAmount;
    for (int i = 0; i < r78::kNumInst; ++i)
        if (pattern.get (var, i, step))
            kit.trigger (i, vel, true);
    currentStep.store (step);
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
