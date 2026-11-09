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
}

juce::AudioProcessorValueTreeState::ParameterLayout RitmoProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", 0.0f, 1.0f, 0.8f));

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
        float l, r;
        kit.process (l, r);
        L[n] = l * gain;
        R[n] = r * gain;
    }
}

juce::AudioProcessorEditor* RitmoProcessor::createEditor() { return new RitmoEditor (*this); }

void RitmoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void RitmoProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RitmoProcessor(); }
