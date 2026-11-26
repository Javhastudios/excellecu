// Presets: de fábrica (en el código) y propios (archivos .excellecu en la carpeta de presets).
//
// Un preset guarda todo lo que suena: las bandas (con sus juegos L/R/M/S y lrFresh) y la
// sección de salida. Formato XML con número de versión; al cargarlo, lo que no venga en el
// archivo vuelve a su valor por defecto, así los presets antiguos siguen cargando igual cuando
// se añadan parámetros nuevos.
#pragma once

#include "PluginProcessor.h"

namespace presets
{
constexpr int FORMAT_VERSION = 1;
inline const char* EXTENSION = ".excellecu";
inline const char* ROOT_TAG = "ExcellecuPreset";

// Carpeta de presets propios
inline juce::File userFolder()
{
    using juce::File;
#if JUCE_MAC
    return File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Javhastudios/Excellecu");
#elif JUCE_WINDOWS
    return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Javhastudios/Excellecu/Presets");
#else
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Javhastudios/Excellecu/Presets");
#endif
}

// ── de fábrica ──────────────────────────────────────────────────────────────
// Una banda descrita en pocas líneas. sets: los juegos que se rellenan; los demás quedan como
// al crear la banda desde la interfaz (L y R copian Estéreo; M y S a 0 dB).
struct FactorySet { int key; double fc, gain, q; int slope = 12; bool on = true; };
struct FactoryBand { int type; int edit; std::vector<FactorySet> sets; };
struct Factory { const char* name; std::vector<FactoryBand> bands; double outL = 0, outR = 0, outM = 0, outS = 0; };

inline const std::vector<Factory>& factoryList()
{
    // TDM: curva de referencia de Javhastudios para mezcla y mastering (creada en el Pro-Q 3 y
    // copiada en Excellecu, capturas del 26/11/2026). Los juegos a 0 dB no suenan; se dejan con
    // la frecuencia que tenían para que el punto aparezca en el mismo sitio.
    static const std::vector<Factory> list {
        { "Plano", {} },
        { "TDM", {
            { eq::Bell, eq::St, { { eq::St, 1550, 1.0, 0.41 } } },
            { eq::HighShelf, eq::St, { { eq::St, 20000, 1.7, 0.49 } } },
            { eq::HighShelf, eq::S, { { eq::St, 388, 0, 2.0 }, { eq::M, 388, 0, 2.0 }, { eq::S, 388, 0.5, 2.0 } } },
            { eq::Bell, eq::M, { { eq::St, 60, 0, 0.62 }, { eq::M, 60, 1.0, 0.62 }, { eq::S, 105, 0, 1.0 } } },
            { eq::Bell, eq::St, { { eq::St, 214, -0.8, 4.28 } } },
        } },
    };
    return list;
}

inline juce::String u8 (const char* t) { return juce::String::fromUTF8 (t); }

// ── conversión estado ⇄ XML ─────────────────────────────────────────────────
inline bool isBandParam (const juce::String& id) { return id.startsWithChar ('b') && id.containsChar ('_'); }
inline int bandOf (const juce::String& id) { return id.substring (1).getIntValue() - 1; }

// Estado actual → XML (solo las bandas que existen y la salida)
inline std::unique_ptr<juce::XmlElement> capture (ExcellecuProcessor& p, const juce::String& name)
{
    auto xml = std::make_unique<juce::XmlElement> (ROOT_TAG);
    xml->setAttribute ("version", FORMAT_VERSION);
    xml->setAttribute ("plugin", "Excellecu " EX_VERSION_STRING);
    xml->setAttribute ("name", name);
    int fresh = 0;
    for (int s = 0; s < eq::MAX_BANDS; ++s) if (p.isLrFresh (s)) fresh |= 1 << s;
    xml->setAttribute ("lrFresh", fresh);
    for (auto* prm : p.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (r == nullptr) continue;
        const auto id = r->getParameterID();
        if (id == "bypass") continue;   // el bypass no es parte del sonido guardado
        if (isBandParam (id))
        {
            const int b = bandOf (id);
            if (p.apvts.getRawParameterValue (ids::band (b, "used"))->load() < 0.5f) continue;
        }
        auto* e = xml->createNewChildElement ("P");
        e->setAttribute ("id", id);
        e->setAttribute ("v", r->convertFrom0to1 (r->getValue()));
    }
    return xml;
}

// XML → valores normalizados de todos los parámetros (en el orden de getParameters()).
// Lo que falta en el XML queda en su valor por defecto. Devuelve false si no es un preset.
inline bool decode (ExcellecuProcessor& p, const juce::XmlElement& xml, std::vector<float>& values, int& lrFresh)
{
    if (! xml.hasTagName (ROOT_TAG)) return false;
    const auto& params = p.getParameters();
    values.resize ((size_t) params.size());
    std::map<juce::String, int> index;
    for (int i = 0; i < params.size(); ++i)
    {
        values[(size_t) i] = params[i]->getDefaultValue();
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (params[i])) index[r->getParameterID()] = i;
    }
    for (auto* e : xml.getChildWithTagNameIterator ("P"))
    {
        auto it = index.find (e->getStringAttribute ("id"));
        if (it == index.end()) continue;   // parámetro de otra versión: se ignora
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (params[it->second]);
        values[(size_t) it->second] = r->convertTo0to1 ((float) e->getDoubleAttribute ("v"));
    }
    lrFresh = xml.getIntAttribute ("lrFresh", 0xfff);
    return true;
}

// Preset de fábrica → XML, con los mismos valores que daría crear las bandas a mano
inline std::unique_ptr<juce::XmlElement> factoryXml (const Factory& f)
{
    auto xml = std::make_unique<juce::XmlElement> (ROOT_TAG);
    xml->setAttribute ("version", FORMAT_VERSION);
    xml->setAttribute ("name", u8 (f.name));
    int fresh = 0xfff;
    auto put = [&] (const juce::String& id, double v) {
        auto* e = xml->createNewChildElement ("P");
        e->setAttribute ("id", id);
        e->setAttribute ("v", v);
    };
    auto slopeIdx = [] (int slope) { for (int i = 0; i < 4; ++i) if (eq::SLOPES[(size_t) i] == slope) return i; return 1; };
    for (size_t b = 0; b < f.bands.size() && b < (size_t) eq::MAX_BANDS; ++b)
    {
        const auto& fb = f.bands[b];
        const int slot = (int) b;
        put (ids::band (slot, "used"), 1);
        put (ids::band (slot, "type"), fb.type);
        put (ids::band (slot, "edit"), fb.edit);
        // Base como BandModel::create: todos los juegos con la frecuencia y ganancia de Estéreo y
        // Q 1; M y S a 0 dB (encendidos si hay ganancia). Luego, los juegos que da el preset.
        const FactorySet* st = nullptr;
        for (const auto& s : fb.sets) if (s.key == eq::St) st = &s;
        for (int k = 0; k < eq::NUM_KEYS; ++k)
        {
            FactorySet s = st ? FactorySet { k, st->fc, st->gain, 1.0 } : FactorySet { k, 1000, 0, 1 };
            if (k == eq::M || k == eq::S) { s.gain = 0; s.on = eq::typeHasGain (fb.type); }
            for (const auto& own : fb.sets) if (own.key == k) s = own;
            if (! eq::typeHasGain (fb.type)) s.gain = 0;
            put (ids::set (slot, k, "on"), s.on ? 1 : 0);
            put (ids::set (slot, k, "fc"), s.fc);
            put (ids::set (slot, k, "gain"), s.gain);
            put (ids::set (slot, k, "q"), s.q);
            put (ids::set (slot, k, "slope"), slopeIdx (s.slope));
        }
        bool hasLr = false;
        for (const auto& s : fb.sets) hasLr = hasLr || s.key == eq::L || s.key == eq::R;
        if (hasLr) fresh &= ~(1 << slot);
    }
    put ("out_l", f.outL);
    put ("out_r", f.outR);
    put ("out_m", f.outM);
    put ("out_s", f.outS);
    xml->setAttribute ("lrFresh", fresh);
    return xml;
}

// ── lista para el menú ──────────────────────────────────────────────────────
struct Entry
{
    juce::String name;
    int factory = -1;      // índice en factoryList() o -1
    juce::File file;       // preset propio
};

inline std::vector<Entry> list()
{
    std::vector<Entry> out;
    const auto& f = factoryList();
    for (int i = 0; i < (int) f.size(); ++i) out.push_back ({ u8 (f[(size_t) i].name), i, {} });
    auto files = userFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + EXTENSION);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });
    for (const auto& file : files) out.push_back ({ file.getFileNameWithoutExtension(), -1, file });
    return out;
}

inline std::unique_ptr<juce::XmlElement> load (const Entry& e)
{
    if (e.factory >= 0) return factoryXml (factoryList()[(size_t) e.factory]);
    return juce::parseXML (e.file);
}

inline bool save (ExcellecuProcessor& p, const juce::File& file, const juce::String& name)
{
    file.getParentDirectory().createDirectory();
    auto xml = capture (p, name);
    return xml->writeTo (file);
}

// Nombre de archivo válido a partir de lo que escribe el usuario
inline juce::String safeFileName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).substring (0, 80);
}
} // namespace presets
