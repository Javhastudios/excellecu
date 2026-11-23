// Formatos de texto de la web (fmtFreq, fmtGain)
#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace fmt
{
inline juce::String freq (double f)
{
    return f >= 1000 ? juce::String (f / 1000, f >= 10000 ? 1 : 2) + " kHz" : (f < 100 ? juce::String (f, 1) : juce::String ((int) std::lround (f))) + " Hz";
}
inline juce::String gain (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
} // namespace fmt
