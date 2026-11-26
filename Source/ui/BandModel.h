// Acciones sobre las bandas desde la interfaz (hilo de mensajes): crear, borrar, cambiar
// tipo y modo, editar el juego actual. Traducción de makeBand / deleteBand / setBandType /
// setBandMode / markEdited de la web. Todo se guarda en los parámetros del plugin.
#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
inline const juce::Colour BAND_COLORS[] = {
    juce::Colour (0xfff7a23e), juce::Colour (0xff3ecfa3), juce::Colour (0xff6b9bff), juce::Colour (0xffff7aa8),
    juce::Colour (0xffc58cff), juce::Colour (0xfff2d24b), juce::Colour (0xff5bc0eb), juce::Colour (0xffff8a5c),
    juce::Colour (0xff9be36b), juce::Colour (0xffe86bd6), juce::Colour (0xff7ee0d0), juce::Colour (0xffd4b483) };

// El color va con el hueco: el hueco nuevo es el primero libre, que es también el primer color sin usar
inline juce::Colour bandColour (int slot) { return BAND_COLORS[slot % eq::MAX_BANDS]; }

inline const char* TYPE_LABEL[] = { "Campana", "Low shelf", "High shelf", "High pass", "Low pass" };
inline const char* TYPE_SHORT[] = { "Campana", "L-Shelf", "H-Shelf", "HP", "LP" };
inline const char* KEY_LABEL[] = { "", "L", "R", "M", "S" };

constexpr double EQ_GAIN_MIN = -20, EQ_GAIN_MAX = 12, EQ_Q_MIN = 0.1, EQ_Q_MAX = 10;
constexpr double MAG_F_MIN = 20, MAG_F_MAX = 30000;   // v0.23: hasta 30 kHz (eje y frecuencia de los puntos)

class BandModel
{
public:
    explicit BandModel (ExcellecuProcessor& p) : proc (p) {}

    eq::Params params() const { return proc.currentParams(); }
    eq::Band band (int slot) const { return proc.currentParams().bands[(size_t) slot]; }

    int count() const
    {
        int n = 0;
        for (int s = 0; s < eq::MAX_BANDS; ++s) if (used (s)) ++n;
        return n;
    }
    bool used (int slot) const { return proc.apvts.getRawParameterValue (ids::band (slot, "used"))->load() > 0.5f; }
    // Número que ve el usuario (1, 2, 3... en orden de hueco)
    int number (int slot) const
    {
        int n = 0;
        for (int s = 0; s <= slot; ++s) if (used (s)) ++n;
        return n;
    }

    // ── cambios de parámetros ──
    juce::RangedAudioParameter* param (const juce::String& id) const { return proc.apvts.getParameter (id); }
    void set (const juce::String& id, float plain) const
    {
        if (auto* p = param (id))
        {
            const float v = p->convertTo0to1 (plain);
            if (p->getValue() != v) { p->beginChangeGesture(); p->setValueNotifyingHost (v); p->endChangeGesture(); }
        }
    }
    // Durante un arrastre: sin abrir y cerrar gesto en cada movimiento
    void setLive (const juce::String& id, float plain) const
    {
        if (auto* p = param (id)) p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }
    void begin (const juce::String& id) const { if (auto* p = param (id)) p->beginChangeGesture(); }
    void end (const juce::String& id) const { if (auto* p = param (id)) p->endChangeGesture(); }

    void writeSet (int slot, int key, const eq::BandSet& s) const
    {
        set (ids::set (slot, key, "on"), s.on ? 1.0f : 0.0f);
        set (ids::set (slot, key, "fc"), (float) s.fc);
        set (ids::set (slot, key, "gain"), (float) s.gain);
        set (ids::set (slot, key, "q"), (float) s.q);
        int si = 1;
        for (int i = 0; i < 4; ++i) if (eq::SLOPES[(size_t) i] == s.slope) si = i;
        set (ids::set (slot, key, "slope"), (float) si);
    }

    // Editar L o R fija sus valores: ya no se recopian de Estéreo
    void markEdited (int slot) const
    {
        if (slot >= 0 && eq::modeGroup (band (slot).edit) == eq::GroupLR) proc.setLrFresh (slot, false);
    }

    // makeBand + createBandAt. Devuelve el hueco o -1 si no hay sitio.
    int create (int type, double fc, double gain) const
    {
        int slot = -1;
        for (int s = 0; s < eq::MAX_BANDS && slot < 0; ++s) if (! used (s)) slot = s;
        if (slot < 0) return -1;
        eq::BandSet base;
        base.fc = juce::jlimit (MAG_F_MIN, MAG_F_MAX, fc);
        base.gain = eq::typeHasGain (type) ? std::round (juce::jlimit (EQ_GAIN_MIN, EQ_GAIN_MAX, gain) * 10) / 10 : 0.0;
        set (ids::band (slot, "type"), (float) type);
        set (ids::band (slot, "edit"), (float) eq::St);
        for (int k : { eq::St, eq::L, eq::R }) writeSet (slot, k, base);
        eq::BandSet ms = base;
        ms.on = eq::typeHasGain (type);
        ms.gain = 0;
        writeSet (slot, eq::M, ms);
        writeSet (slot, eq::S, ms);
        proc.setLrFresh (slot, true);
        set (ids::band (slot, "used"), 1.0f);
        proc.selected = slot;
        return slot;
    }

    void remove (int slot) const
    {
        if (slot < 0 || ! used (slot)) return;
        set (ids::band (slot, "used"), 0.0f);
        if (proc.selected == slot)
        {
            // Como la web: pasa a la banda que ocupa su lugar, o a la anterior
            int next = -1;
            for (int s = slot + 1; s < eq::MAX_BANDS && next < 0; ++s) if (used (s)) next = s;
            for (int s = slot - 1; s >= 0 && next < 0; --s) if (used (s)) next = s;
            proc.selected = next;
        }
    }

    void setType (int slot, int type) const
    {
        auto b = band (slot);
        if (! b.used || b.type == type) return;
        const bool toFilter = eq::typeHasSlope (type);
        for (int k = 0; k < eq::NUM_KEYS; ++k)
        {
            if (k == b.edit || k == eq::St) continue;
            if (! eq::setIsLive (b.type, b.sets[(size_t) k]))
            {
                set (ids::set (slot, k, "on"), toFilter ? 0.0f : 1.0f);
                set (ids::set (slot, k, "gain"), 0.0f);
            }
        }
        if (! eq::typeHasGain (type))
            for (int k = 0; k < eq::NUM_KEYS; ++k) set (ids::set (slot, k, "gain"), 0.0f);
        set (ids::band (slot, "type"), (float) type);
    }

    // Estéreo → juego compartido · L o R → modo L/R · M o S → modo M/S
    void setMode (int slot, int key) const
    {
        auto b = band (slot);
        if (! b.used) return;
        if (eq::modeGroup (key) == eq::GroupLR && eq::modeGroup (b.edit) != eq::GroupLR && proc.isLrFresh (slot))
        {
            writeSet (slot, eq::L, b.sets[eq::St]);
            writeSet (slot, eq::R, b.sets[eq::St]);
        }
        set (ids::band (slot, "edit"), (float) key);
    }

    ExcellecuProcessor& proc;
};
} // namespace ui
