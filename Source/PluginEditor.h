#pragma once

#include "PluginProcessor.h"
#include "ui/BandModel.h"
#include "ui/Meters.h"
#include "ui/PresetBar.h"
#include "ui/Theme.h"
#include "ui/ValueEntry.h"
#include "ui/Views.h"

// Fila de "chips" con las bandas (clic = seleccionar). Va en la cabecera de la gráfica.
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

// Fila de deslizador con nombre y valor encima
struct SliderRow
{
    juce::String name, valueText;
    juce::Slider slider;
    juce::Rectangle<int> label;
    bool visible = true;
    void setup (juce::Component& parent, const juce::String& n);
    void place (int x, int y, int w);
};

// Banda seleccionada, en horizontal: cabecera, tipo y canal, y los deslizadores en fila
class BandPanel : public juce::Component
{
public:
    BandPanel (ExcellecuProcessor&, ui::BandModel&, std::function<void()> onChange);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refresh();
    // Alto que necesita con todas las filas a la vista (para no mover la gráfica al cambiar de banda)
    static int preferredHeight (int width);

private:
    struct Row : SliderRow { std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; juce::String paramId; };
    // Grupo de botones con su etiqueta: tipo, canal, forma, resonancia, codo
    struct Group { juce::String title; juce::Rectangle<int> label; juce::OwnedArray<MsButton>* buttons; bool shown = true; int gapAfter = -1; };
    void bindBand();
    // Coloca los grupos seguidos, bajando de fila si no caben. Con place = false solo mide.
    // Devuelve la y de la última fila.
    static int flow (const std::vector<Group*>& groups, int pad, int width, int y, bool place);

    ExcellecuProcessor& proc;
    ui::BandModel& model;
    std::function<void()> onChange;
    juce::ToggleButton onToggle { "Activa" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    juce::OwnedArray<MsButton> typeButtons, keyButtons, shapeButtons, resButtons, kneeButtons;
    Group typeGroup, keyGroup, shapeGroup, resGroup, kneeGroup;
    ValueEntry entry;
    MsButton deleteButton { "Eliminar banda" }, soloButton { "Escuchar" };
    Row fc, gain, q, slope;
    int boundSlot = -2, boundKey = -1, boundType = -1;
    bool hasBand = false;
    juce::Rectangle<int> headRow;
};

// Ganancia de salida: L/R o M/S, enlace
class OutputPanel : public juce::Component
{
public:
    OutputPanel (ExcellecuProcessor&, ui::BandModel&, std::function<void()> onChange);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    struct OutRow : SliderRow { int which = 0; };
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void valueChanged (OutRow&);
    void gestures (bool begin);
    int outIndex (int which) const;   // which 0/1 → índice 0..3 (L, R, Mid, Side) según el modo
    juce::RangedAudioParameter* outParam (int index) const;

    ExcellecuProcessor& proc;
    ui::BandModel& model;
    std::function<void()> onChange;
    MsButton outLr { "L/R" }, outMs { "M/S" };
    juce::ToggleButton outLink { "Enlazar" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> outLinkAtt;
    OutRow outA, outB;
    bool dragging = false;
    const OutRow* dragRow = nullptr;   // el deslizador que se arrastra (el otro se mueve con él si están enlazados)
    float start[4] {};
    juce::String noteText;
    juce::Rectangle<int> title, note;
    ValueEntry entry;
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
    void layoutMagHeader();

    ExcellecuProcessor& proc;
    // Va antes que los componentes: se consulta mientras se construyen (los miembros se
    // inicializan en el orden en que están declarados)
    bool constructed = false;   // true al terminar el constructor
    ui::BandModel model;
    EqLookAndFeel lnf;
    BandChips chips;
    BandPanel bandPanel;
    OutputPanel outPanel;
    MagChart mag;
    PresetBar presetBar;
    MeterView meters;
    juce::TooltipWindow tooltips { this, 700 };
    juce::OwnedArray<MsButton> newTypeButtons, zoomButtons, anaButtons;   // analizador: Pre, Post, velocidad, inclinación
    ChartData chart;
    eq::Params shownParams;
    double shownRate = 0;
    int shownWidth = 0, shownSelected = -2, shownChipsH = -1, chipsW = 100;
    void syncAnaButtons();
    bool firstChart = true;
    juce::Rectangle<int> moduleArea, magPanel, meterPanel, bandArea, outArea, helpArea, zoomLabel, anaLabel;
    juce::String status;
};
