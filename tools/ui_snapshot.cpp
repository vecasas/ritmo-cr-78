// Genera una captura PNG de la interfaz sin abrir ventanas (para documentación y revisión).
// Uso: ui_snapshot <salida.png> [pagina 0|1] [escala]
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
