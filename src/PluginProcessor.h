#pragma once
#include <JuceHeader.h>
#include "dsp/Voices.h"
#include "seq/Sequencer.h"
#include "fx/Effects.h"

// Ritmo CR-78: caja de ritmos analógica modelada (diseño propio).
// Fase 2: 14 voces sintetizadas con control por voz, disparables por MIDI
// o desde los pads de la interfaz.
class RitmoProcessor : public juce::AudioProcessor, private juce::Timer
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
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Disparo desde la interfaz (hilo de mensajes -> hilo de audio, sin bloqueos).
    void auditionFromUi (int inst, bool accent) { pendingPad[(size_t) inst].store (accent ? 2 : 1); }

    // Secuenciador: patrón en edición (variaciones A, B y Fill) y posición actual.
    r78::Pattern pattern;
    std::atomic<int> currentStep { -1 };   // paso que suena ahora (-1 = parado)
    std::atomic<int> currentVar { 0 };     // variación que suena ahora
    std::atomic<float> compGainReduction { 0.0f }; // dB, para el medidor
    void loadFactoryRhythm (int index);
    static juce::StringArray rhythmNames();

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String pid (int inst, const char* param);

private:
    void updateVoiceParams();
    void timerCallback() override;
    void fireStep (long globalStep);
    juce::String patternToString() const;
    void patternFromString (const juce::String&);

    r78::StepClock clock;
    r78::PlayOptions opts;
    double internalPpq = 0.0, expectedPpq = -1.0;
    bool wasRunning = false;
    int clockSpb = 4;
    int loadedRhythm = -1;
    float accentAmount = 1.0f;

    void updateFxParams();
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    r78::DrumKit kit;
    r78::FxChain fx;
    r78::MasterBus masterBus;
    struct VoiceParamPtrs { std::atomic<float>* level; std::atomic<float>* tune; std::atomic<float>* decay;
                            std::atomic<float>* tone; std::atomic<float>* pan; std::atomic<float>* mute;
                            std::atomic<float>* send; };
    std::array<VoiceParamPtrs, r78::kNumInst> vp {};
    std::atomic<float>* master = nullptr;
    std::array<std::atomic<int>, r78::kNumInst> pendingPad {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RitmoProcessor)
};
