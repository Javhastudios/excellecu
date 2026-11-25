// Barra superior: deshacer / rehacer y presets (anterior, menú con el nombre, siguiente, guardar).
#pragma once

#include "PluginProcessor.h"
#include "Theme.h"
#include "state/Presets.h"

#include <juce_gui_basics/juce_gui_basics.h>

class PresetBar : public juce::Component
{
public:
    PresetBar (ExcellecuProcessor&, std::function<void()> onChange);
    ~PresetBar() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;   // clic en el nombre: menú de presets
    void refresh();                                     // nombre, "modificado" y botones (desde el temporizador)

    void undo();
    void redo();
    bool loadEntry (const presets::Entry&);
    void step (int delta);   // preset anterior / siguiente

    static constexpr int idealWidth = 440;

private:
    void showMenu();
    void saveAs();
    void saveTo (const juce::File&, const juce::String& name);
    void importFile();
    void exportFile();

    ExcellecuProcessor& proc;
    std::function<void()> onChange;
    MsButton undoButton { "Deshacer" }, redoButton { "Rehacer" }, prevButton { "<" }, nextButton { ">" }, saveButton { "Guardar" };
    juce::Rectangle<int> nameBox;
    juce::String shownName;
    bool shownModified = false;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> askName;
};
