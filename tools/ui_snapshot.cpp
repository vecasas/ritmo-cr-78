// Genera una captura PNG de la interfaz sin abrir ventanas (para documentación y revisión).
// Uso: ui_snapshot <salida.png> [pagina 0|1] [escala] [modo 0 fábrica|1 usuario|2 canción]
#include <JuceHeader.h>
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "ui.png"));
    const int page = argc > 2 ? juce::String (argv[2]).getIntValue() : 0;
    const float scale = argc > 3 ? juce::String (argv[3]).getFloatValue() : 1.0f;

    RitmoProcessor proc;
    proc.prepareToPlay (48000, 512);
    // Estado de ejemplo: ritmo New Wave, algunas voces con FX.
    if (auto* p = proc.apvts.getParameter ("rhythm")) p->setValueNotifyingHost (p->convertTo0to1 (7));
    if (auto* p = proc.apvts.getParameter ("sd_send")) p->setValueNotifyingHost (1.0f);
    if (auto* p = proc.apvts.getParameter ("fill")) p->setValueNotifyingHost (p->convertTo0to1 (2));
    proc.loadFactoryRhythm (7);
    proc.currentStep.store (4);

    // Modo usuario o canción: unos patrones de ejemplo y una canción corta.
    // Variable R78_SNAP_CANCEL=n: muestra cancelado el grupo n.
    if (auto* c = std::getenv ("R78_SNAP_CANCEL"))
        if (auto* p = proc.apvts.getParameter ("cancel_" + juce::String (c))) p->setValueNotifyingHost (1.0f);

    const int mode = argc > 4 ? juce::String (argv[4]).getIntValue() : 0;
    if (mode > 0)
    {
        const auto& list = r78::factoryRhythms();
        for (int i : { 0, 1, 2, 4, 7, 12 }) r78::loadRhythm (proc.userPatterns[(size_t) i], list[(size_t) i]);
        const int songDemo[] { 7, 7, 7, 4, 7, 7, 12, 0, RitmoProcessor::kSongLoop };
        for (int i = 0; i < 9; ++i) proc.song[(size_t) i].store (songDemo[i]);
        proc.songLength.store (9);
        proc.songEditPos.store (3);
        if (auto* p = proc.apvts.getParameter ("user_slot")) p->setValueNotifyingHost (p->convertTo0to1 (8));
        if (auto* p = proc.apvts.getParameter ("mode")) p->setValueNotifyingHost (p->convertTo0to1 ((float) mode));
    }

    // R78_SELFTEST=1: comprueba la multisalida y la exportación MIDI y sale.
    if (std::getenv ("R78_SELFTEST") != nullptr)
    {
        auto layout = proc.getBusesLayout();
        layout.outputBuses.getReference (2) = juce::AudioChannelSet::stereo();   // activa la salida 3
        std::printf ("layout ok: %d\n", (int) proc.setBusesLayout (layout));
        if (auto* p = proc.apvts.getParameter ("bd_out")) p->setValueNotifyingHost (p->convertTo0to1 (2));   // bombo -> salida 3
        if (auto* p = proc.apvts.getParameter ("sd_out")) p->setValueNotifyingHost (p->convertTo0to1 (5));   // caja -> salida 6 (apagada)
        proc.prepareToPlay (48000, 512);
        juce::AudioBuffer<float> buf (proc.getTotalNumOutputChannels(), 4800);
        juce::MidiBuffer mb;
        proc.auditionFromUi (0, true);
        proc.processBlock (buf, mb);
        std::printf ("canales %d  principal %.3f  salida3 %.3f\n", buf.getNumChannels(),
                     buf.getRMSLevel (0, 0, 4800), buf.getRMSLevel (2, 0, 4800));
        proc.auditionFromUi (1, true);
        buf.clear();
        proc.processBlock (buf, mb);
        std::printf ("caja: principal %.3f  salida3 %.3f\n", buf.getRMSLevel (0, 0, 4800), buf.getRMSLevel (2, 0, 4800));
        // Salida MIDI y entrada con nota de acento
        if (auto* p = proc.apvts.getParameter ("midi_out")) p->setValueNotifyingHost (1.0f);
        proc.auditionFromUi (3, false);
        buf.clear(); mb.clear();
        proc.processBlock (buf, mb);
        for (const auto m : mb) std::printf ("midi out: %s\n", m.getMessage().getDescription().toRawUTF8());
        const auto f = proc.exportMidiFile (2);
        std::printf ("export %s %lld bytes\n", f.getFullPathName().toRawUTF8(), (long long) f.getSize());
        return 0;
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (MainPanel::kW, MainPanel::kH);
    if (auto* re = dynamic_cast<RitmoEditor*> (ed.get())) re->getPanel().showPage (page);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("%s %dx%d\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
