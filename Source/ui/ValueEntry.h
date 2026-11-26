// Escribir un valor con el teclado (web v0.27): doble clic sobre la cifra de un control abre un
// campo encima; Intro o salir del campo aplica, Esc cancela.
#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

class ValueEntry : public juce::TextEditor
{
public:
    ValueEntry()
    {
        setJustification (juce::Justification::centredRight);
        setFont (theme::font (10.0f));
        setColour (juce::TextEditor::backgroundColourId, theme::bg);
        setColour (juce::TextEditor::textColourId, theme::text);
        setColour (juce::TextEditor::outlineColourId, theme::accent);
        setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
        setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.35f));
        setSelectAllWhenFocused (true);
        setVisible (false);
        onFocusLost = [this] { finish (true); };
    }

    // Abre el campo en r (coordenadas del padre) con el valor actual; apply recibe el texto escrito
    void start (juce::Rectangle<int> r, const juce::String& current, std::function<void (const juce::String&)> applyFn)
    {
        apply = std::move (applyFn);
        open = true;
        setBounds (r);
        setText (current, false);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        selectAll();
    }

    bool isOpen() const { return open; }

    // Que Supr, ⌘Z, etc. no lleguen al plugin mientras se escribe
    bool keyPressed (const juce::KeyPress& k) override
    {
        // Intro y Esc aquí mismo (los avisos de TextEditor llegan más tarde y podrían cerrar
        // un campo abierto después)
        if (k == juce::KeyPress::returnKey) { finish (true); return true; }
        if (k == juce::KeyPress::escapeKey) { finish (false); return true; }
        juce::TextEditor::keyPressed (k);
        return true;
    }

private:
    void finish (bool ok)
    {
        if (! open) return;
        open = false;
        const auto text = getText();
        auto fn = std::move (apply);
        apply = nullptr;
        setVisible (false);
        if (ok && fn) fn (text);
    }

    bool open = false;
    std::function<void (const juce::String&)> apply;
};
