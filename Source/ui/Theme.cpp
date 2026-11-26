#include "Theme.h"

using namespace juce;

// v0.25: letra normal (sin serifa) y 2 px más grande que la del diseño original, que era
// monoespaciada de 8–11 px y costaba leer (comparada con FabFilter, ~12–13 px).
Font theme::font (float size)
{
    return Font (FontOptions (Font::getDefaultSansSerifFontName(), size + 2.0f, Font::plain));
}

static Colour colourProp (const Component& c, const char* name, Colour fallback)
{
    const var& v = c.getProperties()[name];
    return v.isVoid() ? fallback : Colour ((uint32) (int64) v);
}

void MsButton::paintButton (Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const Colour hot = danger ? Colour (0xffff6464) : theme::accent;
    Colour line = theme::border, txt = theme::muted;
    if (live) { line = theme::accent.withAlpha (0.45f); txt = theme::accent.withAlpha (0.75f); }
    if (active)
    {
        g.setColour (theme::accent.withAlpha (0.1f));
        g.fillRoundedRectangle (r, 3.0f);
        line = txt = theme::accent;
    }
    if (highlighted) line = txt = hot;
    g.setColour (line);
    g.drawRoundedRectangle (r, 3.0f, 0.8f);
    g.setColour (txt);
    g.setFont (theme::font (9.0f));
    g.drawText (getButtonText(), getLocalBounds(), Justification::centred, false);
}

EqLookAndFeel::EqLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, theme::bg);
    setColour (Label::textColourId, theme::muted);
    setColour (ScrollBar::thumbColourId, theme::border);
    setColour (TooltipWindow::backgroundColourId, theme::surface);
    setColour (TooltipWindow::textColourId, theme::text);
}

void EqLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                                      Slider::SliderStyle, Slider& s)
{
    const float cy = y + height * 0.5f;
    g.setColour (theme::border);
    g.fillRoundedRectangle ((float) x, cy - 1.0f, (float) width, 2.0f, 1.0f);
    g.setColour (colourProp (s, "thumb", theme::accent));
    g.fillEllipse (sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

void EqLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const float box = 14.0f;
    auto r = Rectangle<float> (0.5f, (b.getHeight() - box) * 0.5f, box, box);
    const Colour accent = colourProp (b, "accent", theme::accent);
    if (b.getToggleState())
    {
        g.setColour (accent);
        g.fillRoundedRectangle (r, 2.5f);
        Path tick;
        tick.startNewSubPath (r.getX() + 3.5f, r.getCentreY());
        tick.lineTo (r.getX() + 6.0f, r.getBottom() - 3.5f);
        tick.lineTo (r.getRight() - 3.0f, r.getY() + 3.5f);
        g.setColour (Colours::white);
        g.strokePath (tick, PathStrokeType (1.8f));
    }
    else
    {
        g.setColour (Colours::white);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (highlighted ? accent : Colour (0xff767676));
        g.drawRoundedRectangle (r, 2.5f, 1.0f);
    }
    g.setColour (theme::muted);
    g.setFont (theme::font (10.0f));
    g.drawText (b.getButtonText(), Rectangle<int> ((int) box + 8, 0, b.getWidth() - (int) box - 8, b.getHeight()),
                Justification::centredLeft, true);
}
