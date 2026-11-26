// Colores y aspecto copiados de la hoja de estilos del prototipo web.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
using juce::Colour;
inline const Colour bg { 0xff0e0e10 };
inline const Colour surface { 0xff18181c };
inline const Colour border { 0xff2a2a30 };
inline const Colour accent { 0xff7c6af7 };
inline const Colour accent2 { 0xff3ecfa3 };
inline const Colour warn { 0xfff7a23e };
inline const Colour text { 0xffe8e8f0 };
inline const Colour muted { 0xffa0a0b4 };   // v0.25: más contraste (antes 0xff6b6b7e)
// Colores de cada sección
inline const Colour eq2 { 0xffff6464 };
inline const Colour ls { 0xff7c6af7 };
inline const Colour hs { 0xffd85a30 };
inline const Colour hp { 0xff3ecfa3 };

juce::Font font (float size);
} // namespace theme

// Botón pequeño tipo .ms-btn (Gauss/IIR, L/R M S)
class MsButton : public juce::Button
{
public:
    explicit MsButton (const juce::String& t) : juce::Button (t) {}
    void setActive (bool v) { if (active != v) { active = v; repaint(); } }
    void setLive (bool v) { if (live != v) { live = v; repaint(); } }   // .ms-live: el otro canal también suena
    void setDanger (bool v) { danger = v; }                               // borde rojo al pasar por encima

protected:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool active = false, live = false, danger = false;
};

class EqLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EqLookAndFeel();

    // Deslizador fino de 2 px con bola de 12 px (color en la propiedad "thumb" del slider)
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 6; }

    // Casilla de 14 px (color en la propiedad "accent" del botón)
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};
