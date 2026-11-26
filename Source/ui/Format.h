// Formatos de texto de la web (fmtFreq, fmtGain)
#pragma once

#include <cmath>
#include <cstring>
#include <juce_core/juce_core.h>

namespace fmt
{
inline juce::String freq (double f)
{
    return f >= 1000 ? juce::String (f / 1000, f >= 10000 ? 1 : 2) + " kHz" : (f < 100 ? juce::String (f, 1) : juce::String ((int) std::lround (f))) + " Hz";
}
inline juce::String gain (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }

// Valor escrito con el teclado (parseNumber de la web v0.27): coma o punto decimal y sufijos
// «1,55k», «1500 Hz», «-2,5 dB», «24 dB/oct». Devuelve NaN si no hay número.
inline double parseNumber (const juce::String& text)
{
    auto t = text.trim().toLowerCase().replaceCharacter (',', '.').removeCharacters (" \t");
    double mult = 1;
    for (const char* suf : { "/oct", "oct", "hz", "db" })
        if (t.endsWith (suf)) { t = t.dropLastCharacters ((int) std::strlen (suf)); break; }
    if (t.endsWith ("db")) t = t.dropLastCharacters (2);
    if (t.endsWith ("k")) { t = t.dropLastCharacters (1); mult = 1000; }
    if (t.isEmpty() || ! t.containsAnyOf ("0123456789")) return std::nan ("");
    const auto num = t.retainCharacters ("0123456789.+-e");
    if (num != t) return std::nan ("");
    const double v = num.getDoubleValue();
    return std::isfinite (v) ? v * mult : std::nan ("");
}
} // namespace fmt
