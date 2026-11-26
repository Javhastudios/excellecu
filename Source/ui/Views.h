// Gráficas: magnitud con puntos editables (drawMag + drawHandles + eventos de la web) y fase.
#pragma once

#include "BandModel.h"

struct ChartData
{
    eq::Params p;
    double sr = 48000;
    int width = 0;
    // Magnitud: una curva por píxel
    struct Curve { std::vector<double> db; juce::Colour colour; juce::String label; bool dashed = false; };
    std::vector<double> plotF;
    std::vector<Curve> curves;
    bool anyActive = false;
    // Fase
    std::vector<double> phaseBefore, phaseAfter, phaseMag;
    double maxErr = 0;
    bool multiRole = false;

    void compute (const eq::Params& params, double sampleRate, int plotWidth);
};

class MagChart : public juce::Component
{
public:
    MagChart (ui::BandModel&, std::function<void()> onChange);

    void setData (const ChartData* d) { data = d; repaint(); }
    void paint (juce::Graphics&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void modifierKeysChanged (const juce::ModifierKeys&) override;

    // Geometría (magGeom de la web)
    struct Geom
    {
        float W, H, l = 40, r = 8, t = 8, b = 24, pw, ph;
        double lo = -13.2, hi = 13.2;   // dB visibles (la escala elegida más un 10 % de margen)
        float toX (double f) const;
        double fromX (float x) const;
        float toY (double db) const;
        double fromY (float y) const;
    };
    Geom geom() const;

    // Escala vertical: ±3, ±6, ±12 o ±24 dB (se guarda con la sesión)
    static constexpr int ZOOMS[] = { 3, 6, 12, 24 };
    int zoomDb() const;
    void setZoomDb (int db);

private:
    struct Handle { int slot, key; eq::BandSet p; int type; float x, y; bool editing, selected; };
    struct Hit { int slot = -1, key = -1; bool valid() const { return slot >= 0; } bool operator== (const Hit& o) const { return slot == o.slot && key == o.key; } };
    struct Drag
    {
        Hit h; bool shape = false, fine = false;
        float x0 = 0, y0 = 0, lastX = 0, lastY = 0;
        double fc0 = 0, gain0 = 0, q0 = 1; int slopeIdx0 = 1;
    };

    std::vector<Handle> handles() const;
    Hit hitAt (juce::Point<float>) const;
    void focus (Hit);
    void rebase (float x, float y, bool fine);
    void updateModifiers (float x, float y, bool shift, bool alt);
    void beginDragGestures (bool begin);
    void changed() { if (onChange) onChange(); }
    void showMenu (const juce::MouseEvent&);

    ui::BandModel& model;
    std::function<void()> onChange;
    const ChartData* data = nullptr;
    Hit hover;
    std::optional<Drag> drag;
    float wheelAccum = 0;
};

class PhaseView : public juce::Component
{
public:
    explicit PhaseView (ui::BandModel& m) : model (m) {}
    void setData (const ChartData* d) { data = d; repaint(); }
    void paint (juce::Graphics&) override;

private:
    ui::BandModel& model;
    const ChartData* data = nullptr;
};
