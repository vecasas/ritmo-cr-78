#pragma once
#include <JuceHeader.h>
#include "dsp/Voices.h"
#include "seq/Sequencer.h"
#include "seq/MidiMap.h"
#include "fx/Effects.h"

// Ritmo CR-78: caja de ritmos analógica modelada (diseño propio).
// Fase 2: 14 voces sintetizadas con control por voz, disparables por MIDI
// o desde los pads de la interfaz.
class RitmoProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    RitmoProcessor();
    static constexpr int kNumAux = 7;   // salidas estéreo extra (2..8)
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
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    // Programas = presets de sonido de fábrica (el DAW los ve en su lista).
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Disparo desde la interfaz (hilo de mensajes -> hilo de audio, sin bloqueos).
    void auditionFromUi (int inst, bool accent) { pendingPad[(size_t) inst].store (accent ? 2 : 1); }
    // Fade in (+1) o fade out (-1) desde la interfaz. El fade in arranca la caja si está parada.
    void requestFade (int dir);
    std::atomic<int> fadeDirection { 0 };   // para los LED: +1, -1 o 0

    // MIDI: mapa de notas (editable con "aprender") y aprendizaje en curso.
    r78::MidiMap midiMap;
    std::atomic<int> learnInst { -1 };      // instrumento esperando nota (-1 = ninguno)
    std::atomic<int> lastMidiNote { -1 };   // última nota recibida, para la interfaz

    // Secuenciador. Modo 0 = ritmos de fábrica (copia editable en `pattern`),
    // modo 1 = patrones de usuario, modo 2 = canción (cadena de patrones de usuario).
    static constexpr int kUserSlots = 99, kSongSteps = 99;
    static constexpr int kSongEnd = r78::kSongEnd, kSongLoop = r78::kSongLoop;
    r78::Pattern pattern;
    std::array<r78::Pattern, kUserSlots> userPatterns;
    std::array<std::atomic<int>, kSongSteps> song {};   // índice de patrón 0..98, o kSongEnd / kSongLoop
    std::atomic<int> songLength { 0 };
    std::atomic<int> songEditPos { 0 };                // paso de canción seleccionado en la interfaz
    std::atomic<int> songPlayPos { -1 };               // paso de canción que suena
    std::atomic<const r78::Pattern*> playingPattern { nullptr };
    std::atomic<int> recordVar { 0 };                  // variación donde graban los pads en tiempo real
    int mode() const { return (int) apvts.getRawParameterValue ("mode")->load(); }
    int userSlot() const { return juce::jlimit (0, kUserSlots - 1, (int) apvts.getRawParameterValue ("user_slot")->load() - 1); }
    r78::Pattern& editPattern();        // el patrón que muestra y edita la rejilla
    bool patternHasNotes (const r78::Pattern&) const;
    std::atomic<int> currentStep { -1 };   // paso que suena ahora (-1 = parado)
    std::atomic<int> currentVar { 0 };     // variación que suena ahora
    std::atomic<float> compGainReduction { 0.0f }; // dB, para el medidor
    void loadFactoryRhythm (int index);
    static juce::StringArray rhythmNames();

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String pid (int inst, const char* param);

    // Exportar a MIDI: escribe un .mid temporal con lo que sonaría (compases del
    // patrón actual, o una pasada de la canción en modo canción).
    juce::File exportMidiFile (int bars);

    // Presets: archivo con el estado completo (sonido, efectos, patrones de
    // usuario, canción y mapa MIDI) en Documentos/Ritmo CR-78/Presets.
    static constexpr const char* kPresetExt = ".r78preset";
    static juce::File presetFolder();
    static juce::Array<juce::File> userPresets();
    bool savePreset (const juce::File&);
    bool loadPreset (const juce::File&);
    juce::String presetName;   // nombre mostrado (hilo de mensajes)

private:
    static BusesProperties makeBuses();
    std::unique_ptr<juce::XmlElement> createStateXml();
    bool loadStateXml (const juce::XmlElement&);
    int currentProgram = 0;
    void updateVoiceParams();
    void timerCallback() override;
    void fireStep (long globalStep);
    static juce::String patternToString (const r78::Pattern&);
    static void patternFromString (r78::Pattern&, const juce::String&);
    const r78::Pattern* patternForStep (long g, long& localStep, bool& songMode);
    void recordPad (int inst, double ppq, float swing);

    r78::SongCursor songCursor;
    std::atomic<bool> stopRequested { false };

    r78::StepClock clock;
    r78::PlayOptions opts;
    double internalPpq = 0.0, expectedPpq = -1.0;
    bool wasRunning = false;
    int clockSpb = 4;
    int loadedRhythm = -1;
    float accentAmount = 1.0f;
    r78::Fader fader;
    std::atomic<int> fadeRequest { 0 };
    std::array<std::atomic<float>*, r78::kCancelGroups> cancelParams {};
    // Salida MIDI del ritmo: golpes de este bloque y notas pendientes de apagar.
    void sendMidiHit (int inst, bool accent, int samplePos);
    juce::MidiBuffer midiOut;
    std::array<int, r78::kNumInst> noteOffIn {};   // muestras hasta el note-off (0 = ninguna)
    std::array<int, r78::kNumInst> noteOutNum {};  // nota enviada, para apagar la misma
    int midiOutCh = 10;
    bool midiOutOn = false;
    int blockPos = 0;
    bool accentHeld = false;                              // muestra actual dentro del bloque

    void updateFxParams();
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    r78::DrumKit kit;
    r78::FxChain fx;
    r78::MasterBus masterBus;
    struct VoiceParamPtrs { std::atomic<float>* level; std::atomic<float>* tune; std::atomic<float>* decay;
                            std::atomic<float>* tone; std::atomic<float>* pan; std::atomic<float>* mute;
                            std::atomic<float>* send; std::atomic<float>* out;
                            std::atomic<float>* extra; };
    std::array<VoiceParamPtrs, r78::kNumInst> vp {};
    std::atomic<float>* master = nullptr;
    std::array<std::atomic<int>, r78::kNumInst> pendingPad {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RitmoProcessor)
};
