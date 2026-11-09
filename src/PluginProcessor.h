#pragma once
#include <JuceHeader.h>
#include "dsp/Voices.h"

// Ritmo CR-78: caja de ritmos analógica modelada (diseño propio).
// Fase 2: 14 voces sintetizadas con control por voz, disparables por MIDI
// o desde los pads de la interfaz.
class RitmoProcessor : public juce::AudioProcessor
{
public:
    RitmoProcessor();
    ~RitmoProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Disparo desde la interfaz (hilo de mensajes -> hilo de audio, sin bloqueos).
    void auditionFromUi (int inst, bool accent) { pendingPad[(size_t) inst].store (accent ? 2 : 1); }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String pid (int inst, const char* param);

private:
    void updateVoiceParams();

    r78::DrumKit kit;
    struct VoiceParamPtrs { std::atomic<float>* level; std::atomic<float>* tune; std::atomic<float>* decay;
                            std::atomic<float>* tone; std::atomic<float>* pan; std::atomic<float>* mute; };
    std::array<VoiceParamPtrs, r78::kNumInst> vp {};
    std::atomic<float>* master = nullptr;
    std::array<std::atomic<int>, r78::kNumInst> pendingPad {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RitmoProcessor)
};
