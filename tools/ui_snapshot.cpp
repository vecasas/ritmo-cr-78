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
