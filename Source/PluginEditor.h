#pragma once

#include "PluginProcessor.h"
#include "ui/BandModel.h"
#include "ui/Meters.h"
#include "ui/PresetBar.h"
#include "ui/Theme.h"
#include "ui/Views.h"

// Fila de "chips" con las bandas (clic = seleccionar)
class BandChips : public juce::Component
{
public:
    BandChips (ui::BandModel& m, std::function<void()> cb) : model (m), onChange (std::move (cb)) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int layoutHeight (int width); // calcula la posición de cada chip

private:
    struct Chip { int slot; juce::Rectangle<int> r; juce::String text; };
    std::vector<Chip> chips;
    ui::BandModel& model;
    std::function<void()> onChange;
};

// Columna de controles (los mismos bloques que la web, en el mismo orden)
class ControlsPanel : public juce::Component
{
public:
    ControlsPanel (ExcellecuProcessor&, ui::BandModel&, std::function<void()> onChange);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();   // textos, botones, visibilidad y a qué parámetros apuntan los controles
    int idealHeight() const { return contentHeight; }

private:
    struct Row
    {
        juce::String name, valueText;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::String paramId;
        juce::Rectangle<int> label;
        bool visible = true;
    };
    void bindBand();
    void setupRow (Row&, const juce::String& name);
    // Salida
    struct OutRow { juce::String name, valueText; juce::Slider slider; juce::Rectangle<int> label; int which = 0; };
    void setupOut();
    void outValueChanged (OutRow&);
    void outGestures (bool begin);
    void syncOut();
    int outIndex (int which) const;   // which 0/1 → índice 0..3 (L, R, Mid, Side) según el modo
    juce::RangedAudioParameter* outParam (int index) const;
    void layoutRow (Row&, int x, int& y, int w, int gapAfter);

    ExcellecuProcessor& proc;
    ui::BandModel& model;
    std::function<void()> onChange;

    BandChips chips;
    juce::ToggleButton onToggle { "Activa" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    juce::OwnedArray<MsButton> typeButtons, keyButtons;
    MsButton deleteButton { "Eliminar banda" };
    Row fc, gain, q, slope;

    // A qué banda/juego/tipo están conectados los controles ahora
    int boundSlot = -2, boundKey = -1, boundType = -1;
    bool hasBand = false;
    juce::String countText;
    MsButton outLr { "L/R" }, outMs { "M/S" };
    juce::ToggleButton outLink { "Enlazar" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> outLinkAtt;
    OutRow outA, outB;
    bool outDragging = false;
    float outStart[4] {};
    juce::String outNoteText;
    juce::Rectangle<int> outTitle, outModeLabel, outNote;

    juce::Rectangle<int> bandsTitle, emptyHint, bandBox, headRow, typeLabel, keyLabel, motorTitle, engineNote, dividers[2];
    juce::String engineNoteText;
    int contentHeight = 800;
};

class ExcellecuEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ExcellecuEditor (ExcellecuProcessor&);
    ~ExcellecuEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void refreshNow(); // tras cualquier cambio hecho desde la interfaz

private:
    void timerCallback() override;

    ExcellecuProcessor& proc;
    // Va antes que los componentes: se consulta mientras se construyen (los miembros se
    // inicializan en el orden en que están declarados)
    bool constructed = false;   // true al terminar el constructor
    ui::BandModel model;
    EqLookAndFeel lnf;
    ControlsPanel controls;
    juce::Viewport viewport;
    MagChart mag;
    PhaseView phase;
    PresetBar presetBar;
    MeterView meters;
    juce::TooltipWindow tooltips { this, 700 };
    juce::OwnedArray<MsButton> newTypeButtons;
    ChartData chart;
    eq::Params shownParams;
    double shownRate = 0;
    int shownWidth = 0, shownSelected = -2;
    bool firstChart = true;
    juce::Rectangle<int> moduleArea, magPanel, phasePanel, meterPanel, helpArea;
    juce::String status;
};
