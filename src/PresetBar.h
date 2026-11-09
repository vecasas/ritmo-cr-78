#pragma once
#include "PluginProcessor.h"
#include "ui/VintageLookAndFeel.h"

// Barra de presets: anterior / siguiente, nombre (clic = menú) y Guardar.
// La lista recorre primero los presets de sonido de fábrica y luego los del usuario.
class PresetBar : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar (RitmoProcessor& p) : proc (p)
    {
        for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &nameBtn })
        {
            b->getProperties().set ("keyColour", (juce::int64) (b == &nameBtn ? juce::Colour (0xff2b2925) : juce::Colour (0xff8c8a82)).getARGB());
            addAndMakeVisible (*b);
        }
        nameBtn.setColour (juce::TextButton::textColourOffId, vintage::mustard);
        prevBtn.onClick = [this] { step (-1); };
        nextBtn.onClick = [this] { step (+1); };
        nameBtn.onClick = [this] { showMenu(); };
        saveBtn.onClick = [this] { save(); };
        nameBtn.setTooltip (u ("Elegir preset. Los de fábrica cambian solo el sonido; los tuyos guardan también patrones, canción y mapa MIDI."));
        saveBtn.setTooltip (u ("Guarda todo (sonido, efectos, patrones de usuario, canción y mapa MIDI) como preset"));
        startTimerHz (4);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (vintage::cream.withAlpha (0.6f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("PRESET", 0, 0, 56, getHeight(), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (58);
        prevBtn.setBounds (r.removeFromLeft (30));
        r.removeFromLeft (4);
        saveBtn.setBounds (r.removeFromRight (90));
        r.removeFromRight (4);
        nextBtn.setBounds (r.removeFromRight (30));
        r.removeFromRight (4);
        nameBtn.setBounds (r);
    }

private:
    static juce::String u (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    // Lista completa: índices < numPrograms son de fábrica; el resto, archivos.
    int listSize() const { return proc.getNumPrograms() + files.size(); }
    void select (int idx)
    {
        const int nf = proc.getNumPrograms();
        if (idx < nf) proc.setCurrentProgram (idx);
        else if (idx - nf < files.size()) proc.loadPreset (files[idx - nf]);
        current = idx;
        timerCallback();
    }
    void step (int d)
    {
        files = RitmoProcessor::userPresets();
        const int n = listSize();
        if (n == 0) return;
        select (((current < 0 ? 0 : current + d) % n + n) % n);
    }

    void showMenu()
    {
        files = RitmoProcessor::userPresets();
        juce::PopupMenu m, factory, mine;
        const int nf = proc.getNumPrograms();
        for (int i = 0; i < nf; ++i) factory.addItem (1 + i, proc.getProgramName (i), true, i == current);
        for (int i = 0; i < files.size(); ++i) mine.addItem (1 + nf + i, files[i].getFileNameWithoutExtension(), true, nf + i == current);
        m.addSectionHeader (u ("De fábrica (solo sonido)"));
        m.addSubMenu (u ("Fábrica"), factory);
        m.addSectionHeader (u ("Tuyos"));
        if (files.isEmpty()) m.addItem (-1, u ("(todavía no hay ninguno)"), false);
        else m.addSubMenu (u ("Mis presets"), mine);
        m.addSeparator();
        m.addItem (9001, u ("Guardar como…"));
        m.addItem (9002, u ("Cargar archivo…"));
        m.addItem (9003, u ("Abrir la carpeta de presets"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameBtn), [this] (int r)
        {
            if (r == 9001) save();
            else if (r == 9002) loadFromFile();
            else if (r == 9003) RitmoProcessor::presetFolder().startAsProcess();
            else if (r >= 1) select (r - 1);
        });
    }

    void save()
    {
        chooser = std::make_unique<juce::FileChooser> (u ("Guardar preset"),
            RitmoProcessor::presetFolder().getChildFile ((proc.presetName.isNotEmpty() ? proc.presetName : juce::String ("Mi preset"))
                                                         + RitmoProcessor::kPresetExt),
            "*" + juce::String (RitmoProcessor::kPresetExt));
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            proc.savePreset (f.withFileExtension (RitmoProcessor::kPresetExt));
            files = RitmoProcessor::userPresets();
            current = proc.getNumPrograms() + files.indexOf (f.withFileExtension (RitmoProcessor::kPresetExt));
            timerCallback();
        });
    }

    void loadFromFile()
    {
        chooser = std::make_unique<juce::FileChooser> (u ("Cargar preset"), RitmoProcessor::presetFolder(),
                                                       "*" + juce::String (RitmoProcessor::kPresetExt));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile() && proc.loadPreset (f)) { current = -1; timerCallback(); }
        });
    }

    void timerCallback() override
    {
        const auto name = proc.presetName.isNotEmpty() ? proc.presetName : u ("Sin título");
        if (nameBtn.getButtonText() != name) nameBtn.setButtonText (name);
    }

    RitmoProcessor& proc;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Guardar" }, nameBtn;
    juce::Array<juce::File> files;
    int current = -1;
    std::unique_ptr<juce::FileChooser> chooser;
};
