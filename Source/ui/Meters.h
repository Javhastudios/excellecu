// Medidores de entrada (antes del EQ) y salida (después de la ganancia de salida).
// Por canal: pico (barra clara, caída de 20 dB/s), RMS de ~300 ms (barra oscura), pico
// retenido 1,5 s y piloto de saturación (≥ 0 dBFS) que se queda encendido hasta hacer clic.
// RMS: AES-17 (por defecto, +3,01 dB: un seno marca lo mismo en pico y en RMS) o sin ponderar.
#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

class MeterView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit MeterView (ExcellecuProcessor&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;   // clic: apaga la saturación y el máximo

    static constexpr float DB_MIN = -60.0f, DB_MAX = 6.0f;

    struct Channel
    {
        float peakDb = -100, rmsDb = -100, holdDb = -100, maxDb = -100;
        double holdTime = 0;
        bool clip = false;
        std::array<float, 10> ss {};   // sumas de cuadrados de los últimos 10 refrescos (~330 ms)
        std::array<int, 10> n {};
        int pos = 0;
    };
    const Channel& channel (int group, int c) const { return groups[(size_t) group][(size_t) c]; }
    void tick();   // recoge del procesador (lo llama el temporizador; público para las pruebas)
    void resized() override;

    static constexpr float AES17_OFFSET = 3.0103f;   // 20·log10(√2)
    bool aes17() const;
    void setAes17 (bool on);
    float shownRms (const Channel& c) const { return c.rmsDb + (aes17() ? AES17_OFFSET : 0.0f); }
    // Cifra de RMS bajo cada grupo (canal más alto), refrescada 4 veces por segundo para poder leerla
    float rmsReadout (int group) const { return rmsShown[(size_t) group]; }

private:
    void timerCallback() override { tick(); }
    float yFor (float db, juce::Rectangle<float> bar) const;

    ExcellecuProcessor& proc;
    MsButton aesButton { "AES-17" }, flatButton { "Sin ponderar" };
    std::array<std::array<Channel, 2>, 2> groups;   // [entrada/salida][canal]
    int channels = 2;
    double lastTick = 0, lastReadout = 0;
    std::array<float, 2> rmsShown { -120.0f, -120.0f };
};
