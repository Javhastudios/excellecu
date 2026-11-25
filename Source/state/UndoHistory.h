// Deshacer y rehacer propios del plugin.
//
// Solo se apuntan los cambios hechos desde la interfaz: se reconocen porque abren y cierran un
// "gesto" de parámetro (la automatización del DAW no lo hace). Cada paso guarda, de los
// parámetros que se tocaron, el valor de antes y el de después, así que deshacer no pisa los
// demás parámetros (por ejemplo, los que esté moviendo una automatización).
//
// Agrupación: todo lo que se cambia dentro de una misma llamada de la interfaz (crear una banda
// toca ~30 parámetros) y todo un arrastre (de mouseDown a mouseUp) queda en un solo paso. Los
// cambios seguidos del mismo control (la rueda, por ejemplo) se juntan si llegan en menos de
// MERGE_MS.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>

class UndoHistory : private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater
{
public:
    static constexpr int MAX_STEPS = 100;
    static constexpr double MERGE_MS = 400.0;

    // extra: estado que no son parámetros pero va con cada paso (lrFresh de la web)
    struct Extra
    {
        std::function<int()> get;
        std::function<void (int)> set;
    };

    UndoHistory (juce::AudioProcessor& p, Extra e) : proc (p), extra (std::move (e))
    {
        for (auto* prm : proc.getParameters()) prm->addListener (this);
    }
    ~UndoHistory() override
    {
        cancelPendingUpdate();
        for (auto* prm : proc.getParameters()) prm->removeListener (this);
    }

    bool canUndo() const { return ! undoSteps.empty(); }
    bool canRedo() const { return ! redoSteps.empty(); }
    int undoCount() const { return (int) undoSteps.size(); }
    int redoCount() const { return (int) redoSteps.size(); }

    // selected: banda seleccionada en la interfaz, para devolver la selección al deshacer
    void setSelectionAccess (std::function<int()> get, std::function<void (int)> set)
    {
        getSelected = std::move (get);
        setSelected = std::move (set);
    }

    bool undo() { return step (undoSteps, redoSteps, true); }
    bool redo() { return step (redoSteps, undoSteps, false); }

    // Al cargar una sesión del DAW el historial anterior ya no tiene sentido
    void clear()
    {
        cancelPendingUpdate();
        undoSteps.clear();
        redoSteps.clear();
        pending = {};
        openGestures = 0;
    }

    // Cierra ya el paso en curso (si no hay gestos abiertos). Útil en pruebas y antes de
    // deshacer, para que un cambio recién hecho cuente como paso.
    void flush()
    {
        if (isUpdatePending()) { cancelPendingUpdate(); commit(); }
    }

private:
    struct Step
    {
        std::map<int, std::pair<float, float>> values;   // índice → (antes, después), normalizados
        int extraBefore = 0, extraAfter = 0;
        int selBefore = -1, selAfter = -1;
        double time = 0;
    };
    struct Pending
    {
        bool open = false;
        std::map<int, float> before;
        int extraBefore = 0, selBefore = -1;
    };

    void parameterValueChanged (int, float) override {}

    void parameterGestureChanged (int index, bool starting) override
    {
        // Solo la interfaz (hilo de mensajes) y nunca mientras se deshace
        if (restoring || ! juce::MessageManager::existsAndIsCurrentThread()) return;
        if (starting)
        {
            if (! pending.open)
            {
                pending.open = true;
                pending.extraBefore = extra.get ? extra.get() : 0;
                pending.selBefore = getSelected ? getSelected() : -1;
            }
            if (pending.before.find (index) == pending.before.end())
                pending.before[index] = proc.getParameters()[index]->getValue();
            ++openGestures;
        }
        else
        {
            openGestures = juce::jmax (0, openGestures - 1);
            if (openGestures == 0) triggerAsyncUpdate();
        }
    }

    void handleAsyncUpdate() override { commit(); }

    void commit()
    {
        if (openGestures > 0 || ! pending.open) return;   // sigue un arrastre: se cierra al acabar
        Step s;
        s.extraBefore = pending.extraBefore;
        s.extraAfter = extra.get ? extra.get() : 0;
        s.selBefore = pending.selBefore;
        s.selAfter = getSelected ? getSelected() : -1;
        s.time = juce::Time::getMillisecondCounterHiRes();
        const auto& params = proc.getParameters();
        for (const auto& [idx, before] : pending.before)
        {
            const float after = params[idx]->getValue();
            if (after != before) s.values[idx] = { before, after };
        }
        pending = {};
        if (s.values.empty() && s.extraBefore == s.extraAfter) return;

        // Mismo control tocado otra vez enseguida (rueda, teclado): se suma al paso anterior
        if (! undoSteps.empty() && redoSteps.empty())
        {
            auto& last = undoSteps.back();
            bool sameKeys = last.values.size() == s.values.size() && last.extraAfter == s.extraBefore;
            for (auto a = last.values.begin(), b = s.values.begin(); sameKeys && b != s.values.end(); ++a, ++b)
                sameKeys = a->first == b->first && a->second.second == b->second.first;
            if (sameKeys && s.time - last.time < MERGE_MS)
            {
                for (auto& [idx, v] : s.values) last.values[idx].second = v.second;
                last.extraAfter = s.extraAfter;
                last.selAfter = s.selAfter;
                last.time = s.time;
                return;
            }
        }
        undoSteps.push_back (std::move (s));
        if ((int) undoSteps.size() > MAX_STEPS) undoSteps.erase (undoSteps.begin());
        redoSteps.clear();
    }

    bool step (std::vector<Step>& from, std::vector<Step>& to, bool back)
    {
        flush();
        if (openGestures > 0 || from.empty()) return false;
        Step s = std::move (from.back());
        from.pop_back();
        const auto& params = proc.getParameters();
        restoring = true;
        for (const auto& [idx, v] : s.values)
        {
            auto* prm = params[idx];
            prm->beginChangeGesture();   // el DAW lo ve como un cambio normal (y lo graba si escribe automatización)
            prm->setValueNotifyingHost (back ? v.first : v.second);
            prm->endChangeGesture();
        }
        if (extra.set) extra.set (back ? s.extraBefore : s.extraAfter);
        if (setSelected) setSelected (back ? s.selBefore : s.selAfter);
        restoring = false;
        s.time = 0;   // un paso deshecho no se junta con el siguiente cambio
        to.push_back (std::move (s));
        return true;
    }

    juce::AudioProcessor& proc;
    Extra extra;
    std::function<int()> getSelected;
    std::function<void (int)> setSelected;
    std::vector<Step> undoSteps, redoSteps;
    Pending pending;
    int openGestures = 0;
    bool restoring = false;
};
