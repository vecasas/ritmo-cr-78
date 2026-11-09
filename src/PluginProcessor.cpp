#include "PluginProcessor.h"
#include "PluginEditor.h"

Ritmo78Processor::Ritmo78Processor()
    : AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout Ritmo78Processor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "master", 1 }, "Master", 0.0f, 1.0f, 0.8f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tempo", 1 }, "Tempo", 40.0f, 240.0f, 120.0f));
    return layout;
}

bool Ritmo78Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void Ritmo78Processor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    phase = 0.0;
    env = 0.0f;
}

void Ritmo78Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const float master = apvts.getRawParameterValue ("master")->load();
    const float decay = std::exp (-1.0f / (0.08f * (float) sr)); // ~80 ms
    const double inc = juce::MathConstants<double>::twoPi * 880.0 / sr;

    auto midiIt = midi.cbegin();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        while (midiIt != midi.cend() && (*midiIt).samplePosition <= i)
        {
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())
            {
                env = msg.getFloatVelocity();
                phase = 0.0;
            }
            ++midiIt;
        }

        const float s = (float) std::sin (phase) * env * master;
        phase += inc;
        env *= decay;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, s);
    }
}

juce::AudioProcessorEditor* Ritmo78Processor::createEditor()
{
    return new Ritmo78Editor (*this);
}

void Ritmo78Processor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void Ritmo78Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Ritmo78Processor();
}
