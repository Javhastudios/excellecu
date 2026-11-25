#include "PresetBar.h"

using namespace juce;

namespace
{
String u8 (const char* t) { return String::fromUTF8 (t); }
}

PresetBar::PresetBar (ExcellecuProcessor& p, std::function<void()> cb) : proc (p), onChange (std::move (cb))
{
    for (auto* b : { &undoButton, &redoButton, &prevButton, &nextButton, &saveButton }) addAndMakeVisible (b);
#if JUCE_MAC
    undoButton.setTooltip (u8 ("Deshacer (⌘Z)"));
    redoButton.setTooltip (u8 ("Rehacer (⌘⇧Z)"));
#else
    undoButton.setTooltip ("Deshacer (Ctrl+Z)");
    redoButton.setTooltip (u8 ("Rehacer (Ctrl+Mayús+Z o Ctrl+Y)"));
#endif
    prevButton.setTooltip ("Preset anterior");
    nextButton.setTooltip ("Preset siguiente");
    saveButton.setTooltip ("Guardar como preset propio");
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    saveButton.onClick = [this] { saveAs(); };
    refresh();
}

PresetBar::~PresetBar() = default;

void PresetBar::undo() { if (proc.history.undo() && onChange) onChange(); refresh(); }
void PresetBar::redo() { if (proc.history.redo() && onChange) onChange(); refresh(); }

void PresetBar::resized()
{
    auto r = getLocalBounds();
    const int h = 17, y = (r.getHeight() - h) / 2;
    int x = 0;
    undoButton.setBounds (x, y, 58, h); x += 61;
    redoButton.setBounds (x, y, 56, h); x += 56 + 14;
    prevButton.setBounds (x, y, 20, h); x += 23;
    const int nameW = r.getWidth() - x - 23 - 8 - 58;
    nameBox = { x, y, nameW, h }; x += nameW + 3;
    nextButton.setBounds (x, y, 20, h); x += 20 + 8;
    saveButton.setBounds (x, y, 58, h);
}

void PresetBar::refresh()
{
    undoButton.setAlpha (proc.history.canUndo() ? 1.0f : 0.35f);
    redoButton.setAlpha (proc.history.canRedo() ? 1.0f : 0.35f);
    const auto name = proc.presetName();
    const bool mod = proc.presetModified();
    if (name != shownName || mod != shownModified) { shownName = name; shownModified = mod; repaint(); }
}

void PresetBar::paint (Graphics& g)
{
    auto r = nameBox.toFloat().reduced (0.5f);
    g.setColour (Colours::white.withAlpha (0.03f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (r, 3.0f, 0.8f);
    g.setFont (theme::font (10.0f));
    g.setColour (theme::text);
    const String text = shownName + (shownModified ? u8 (" · modificado") : String());
    g.drawText (text, nameBox.reduced (8, 0).withTrimmedRight (10), Justification::centredLeft, true);
    // Triángulo del menú
    Path tri;
    const float cx = (float) nameBox.getRight() - 10, cy = (float) nameBox.getCentreY();
    tri.addTriangle (cx - 3.5f, cy - 2, cx + 3.5f, cy - 2, cx, cy + 2.5f);
    g.setColour (theme::muted);
    g.fillPath (tri);
}

void PresetBar::mouseUp (const MouseEvent& e)
{
    if (nameBox.contains (e.getPosition())) showMenu();
}

bool PresetBar::loadEntry (const presets::Entry& e)
{
    auto xml = presets::load (e);
    std::vector<float> values;
    int fresh = 0xfff;
    if (xml == nullptr || ! presets::decode (proc, *xml, values, fresh))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Excellecu",
                                          u8 ("No se ha podido leer el preset «") + e.name + u8 ("»."));
        return false;
    }
    proc.applyPreset (values, fresh, e.name);
    if (onChange) onChange();
    refresh();
    return true;
}

void PresetBar::step (int delta)
{
    const auto all = presets::list();
    if (all.empty()) return;
    int cur = -1;
    for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i].name == proc.presetName()) cur = i;
    const int n = (int) all.size();
    const int next = cur < 0 ? (delta > 0 ? 0 : n - 1) : ((cur + delta) % n + n) % n;
    loadEntry (all[(size_t) next]);
}

void PresetBar::showMenu()
{
    const auto all = presets::list();
    PopupMenu m;
    m.addSectionHeader (u8 ("De fábrica"));
    bool userHeader = false;
    for (const auto& e : all)
    {
        if (e.factory < 0 && ! userHeader) { m.addSeparator(); m.addSectionHeader ("Propios"); userHeader = true; }
        m.addItem (e.name, true, e.name == proc.presetName(), [this, e] { loadEntry (e); });
    }
    m.addSeparator();
    m.addItem (u8 ("Guardar como…"), [this] { saveAs(); });
    m.addItem (u8 ("Importar archivo…"), [this] { importFile(); });
    m.addItem (u8 ("Exportar a archivo…"), [this] { exportFile(); });
    m.addItem ("Abrir la carpeta de presets", [] {
        auto dir = presets::userFolder();
        dir.createDirectory();
        dir.startAsProcess();
    });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (nameBox))
                         .withMinimumWidth (nameBox.getWidth()));
}

void PresetBar::saveTo (const File& file, const String& name)
{
    if (! presets::save (proc, file, name))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Excellecu",
                                          u8 ("No se ha podido guardar en ") + file.getFullPathName());
        return;
    }
    proc.setPresetSaved (name);
    refresh();
}

void PresetBar::saveAs()
{
    const String current = proc.presetName();
    askName = std::make_unique<AlertWindow> ("Guardar preset", u8 ("Nombre del preset:"), MessageBoxIconType::NoIcon, this);
    askName->addTextEditor ("name", current == "Plano" ? String() : current);
    askName->addButton ("Guardar", 1, KeyPress (KeyPress::returnKey));
    askName->addButton ("Cancelar", 0, KeyPress (KeyPress::escapeKey));
    SafePointer<PresetBar> safe (this);
    askName->enterModalState (true, ModalCallbackFunction::create ([safe] (int result) {
        if (safe == nullptr || safe->askName == nullptr) return;
        const String typed = safe->askName->getTextEditorContents ("name").trim();
        safe->askName.reset();
        if (result != 1 || typed.isEmpty()) return;
        const String fileName = presets::safeFileName (typed);
        if (fileName.isEmpty()) return;
        const File file = presets::userFolder().getChildFile (fileName + presets::EXTENSION);
        bool isFactory = false;
        for (const auto& f : presets::factoryList()) isFactory = isFactory || String::fromUTF8 (f.name) == fileName;
        if (isFactory)
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Excellecu",
                                              u8 ("«") + fileName + u8 ("» es un preset de fábrica. Elige otro nombre."));
            return;
        }
        if (file.existsAsFile())
        {
            AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Guardar preset",
                                          u8 ("Ya existe «") + fileName + u8 ("». ¿Reemplazarlo?"), "Reemplazar", "Cancelar", safe.getComponent(),
                                          ModalCallbackFunction::create ([safe, file, fileName] (int ok) {
                                              if (safe != nullptr && ok == 1) safe->saveTo (file, fileName);
                                          }));
            return;
        }
        safe->saveTo (file, fileName);
    }), false);
}

void PresetBar::importFile()
{
    chooser = std::make_unique<FileChooser> ("Importar preset", File::getSpecialLocation (File::userDocumentsDirectory),
                                             String ("*") + presets::EXTENSION);
    SafePointer<PresetBar> safe (this);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [safe] (const FileChooser& fc) {
        if (safe == nullptr) return;
        const File src = fc.getResult();
        if (! src.existsAsFile()) return;
        // Se copia a la carpeta de presets para que aparezca en la lista
        const File dir = presets::userFolder();
        dir.createDirectory();
        File dst = dir.getChildFile (src.getFileName());
        if (! src.isAChildOf (dir))
        {
            if (dst.existsAsFile()) dst = dst.getNonexistentSibling (false);
            src.copyFileTo (dst);
        }
        else dst = src;
        safe->loadEntry ({ dst.getFileNameWithoutExtension(), -1, dst });
    });
}

void PresetBar::exportFile()
{
    const String name = proc.presetName();
    chooser = std::make_unique<FileChooser> ("Exportar preset",
                                             File::getSpecialLocation (File::userDocumentsDirectory)
                                                 .getChildFile (presets::safeFileName (name) + presets::EXTENSION),
                                             String ("*") + presets::EXTENSION);
    SafePointer<PresetBar> safe (this);
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [safe, name] (const FileChooser& fc) {
        if (safe == nullptr) return;
        File f = fc.getResult();
        if (f == File()) return;
        if (! f.hasFileExtension (presets::EXTENSION)) f = f.withFileExtension (presets::EXTENSION);
        if (! presets::save (safe->proc, f, name))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Excellecu",
                                              u8 ("No se ha podido guardar en ") + f.getFullPathName());
    });
}
