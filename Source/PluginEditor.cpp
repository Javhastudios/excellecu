#include "PluginEditor.h"
#include "ui/Format.h"

using namespace juce;

namespace
{
String u8 (const char* t) { return String::fromUTF8 (t); }
constexpr int panelW = 240;

void panelTitle (Graphics& g, Rectangle<int> r, const String& text, Colour c = theme::muted)
{
    g.setFont (theme::font (9.0f));
    g.setColour (c);
    g.drawText (text, r, Justification::centredLeft, true);
    g.setColour (theme::border);
    g.fillRect ((float) r.getX(), (float) r.getBottom() + 4, (float) r.getWidth(), 0.6f);
}
} // namespace

//==============================================================================
int BandChips::layoutHeight (int width)
{
    chips.clear();
    const auto prm = model.params();
    int x = 0, y = 0;
    const int h = 17;
    for (int s = 0; s < eq::MAX_BANDS; ++s)
    {
        const auto& b = prm.bands[(size_t) s];
        if (! b.used) continue;
        String text = String (model.number (s)) + " " + ui::TYPE_SHORT[b.type];
        if (b.edit != eq::St) text << " " << (eq::modeGroup (b.edit) == eq::GroupLR ? "L/R" : "M/S");
        const int w = 6 + 7 + 4 + text.length() * 6 + 6;
        if (x > 0 && x + w > width) { x = 0; y += h + 4; }
        chips.push_back ({ s, { x, y, w, h }, text });
        x += w + 4;
    }
    return chips.empty() ? 4 : y + h;
}

void BandChips::paint (Graphics& g)
{
    const auto prm = model.params();
    for (const auto& c : chips)
    {
        const auto& b = prm.bands[(size_t) c.slot];
        bool live = false;
        const auto grp = eq::modeGroup (b.edit);
        for (int k = 0; k < eq::NUM_KEYS; ++k)
        {
            const bool active = grp == eq::GroupSt ? k == eq::St : grp == eq::GroupLR ? (k == eq::L || k == eq::R) : (k == eq::M || k == eq::S);
            if (active && b.sets[(size_t) k].on) live = true;
        }
        const bool sel = c.slot == model.proc.selected;
        const float a = live ? 1.0f : 0.45f;
        auto r = c.r.toFloat().reduced (0.5f);
        if (sel) { g.setColour (Colours::white.withAlpha (0.05f * a)); g.fillRoundedRectangle (r, 3.0f); }
        g.setColour ((sel ? theme::muted : theme::border).withMultipliedAlpha (a));
        g.drawRoundedRectangle (r, 3.0f, 0.6f);
        g.setColour (ui::bandColour (c.slot).withMultipliedAlpha (a));
        g.fillEllipse ((float) c.r.getX() + 6, (float) c.r.getCentreY() - 3.5f, 7, 7);
        g.setColour ((sel ? theme::text : theme::muted).withMultipliedAlpha (a));
        g.setFont (theme::font (9.0f));
        g.drawText (c.text, c.r.withTrimmedLeft (17), Justification::centredLeft, false);
    }
}

void BandChips::mouseUp (const MouseEvent& e)
{
    for (const auto& c : chips)
        if (c.r.contains (e.getPosition())) { model.proc.selected = c.slot; if (onChange) onChange(); return; }
}

//==============================================================================
void SliderRow::setup (Component& parent, const String& n)
{
    name = n;
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    parent.addAndMakeVisible (slider);
}

void SliderRow::place (int x, int y, int w)
{
    label = { x, y, w, 14 };
    slider.setBounds (x - 6, y + 16, w + 12, 14);
    slider.setVisible (visible);
}

static void paintPanelFrame (Graphics& g, Rectangle<int> r, Colour tint)
{
    auto f = r.toFloat().reduced (0.5f);
    g.setColour (tint.withAlpha (0x0d / 255.0f));
    g.fillRoundedRectangle (f, 4.0f);
    g.setColour (tint.withAlpha (0x55 / 255.0f));
    g.drawRoundedRectangle (f, 4.0f, 0.6f);
}

//==============================================================================
BandPanel::BandPanel (ExcellecuProcessor& p, ui::BandModel& m, std::function<void()> cb)
    : proc (p), model (m), onChange (std::move (cb))
{
    addAndMakeVisible (onToggle);
    for (int t = 0; t < eq::NUM_TYPES; ++t)
    {
        auto* b = typeButtons.add (new MsButton (ui::TYPE_SHORT[t]));
        b->onClick = [this, t] { if (proc.selected >= 0) { model.setType (proc.selected, t); onChange(); } };
        addAndMakeVisible (b);
    }
    const char* keyText[] = { u8"Estéreo", "L", "R", "M", "S" };
    for (int k = 0; k < eq::NUM_KEYS; ++k)
    {
        auto* b = keyButtons.add (new MsButton (String::fromUTF8 (keyText[k])));
        b->onClick = [this, k] { if (proc.selected >= 0) { model.setMode (proc.selected, k); onChange(); } };
        addAndMakeVisible (b);
    }
    deleteButton.setDanger (true);
    deleteButton.onClick = [this] { if (proc.selected >= 0) { model.remove (proc.selected); onChange(); } };
    addAndMakeVisible (deleteButton);
    onToggle.onClick = [this] { if (proc.selected >= 0) { model.markEdited (proc.selected); onChange(); } };

    fc.setup (*this, "Frecuencia");
    gain.setup (*this, "Ganancia");
    q.setup (*this, "Factor Q");
    slope.setup (*this, "Pendiente");
    for (Row* r : { &fc, &gain, &q, &slope })
    {
        r->slider.setDoubleClickReturnValue (false, 0.0);
        r->slider.onDragStart = [this] { if (proc.selected >= 0) model.markEdited (proc.selected); };
    }
    refresh();
}

void BandPanel::bindBand()
{
    const int sel = proc.selected;
    const auto b = sel >= 0 ? model.band (sel) : eq::Band();
    const bool exists = sel >= 0 && b.used;
    if (exists == hasBand && sel == boundSlot && b.edit == boundKey && b.type == boundType) return;
    hasBand = exists; boundSlot = sel; boundKey = b.edit; boundType = b.type;

    onAtt.reset();
    for (Row* r : { &fc, &gain, &q, &slope }) r->att.reset();
    if (! exists) return;

    const Colour col = ui::bandColour (sel);
    auto attach = [&] (Row& r, const char* field) {
        r.paramId = ids::set (sel, b.edit, field);
        r.slider.getProperties().set ("thumb", (int64) col.getARGB());
        r.att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, r.paramId, r.slider);
    };
    attach (fc, "fc");
    attach (gain, "gain");
    attach (q, "q");
    attach (slope, "slope");
    onToggle.getProperties().set ("accent", (int64) col.getARGB());
    onAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::set (sel, b.edit, "on"), onToggle);
}

void BandPanel::refresh()
{
    const int oldSlot = boundSlot, oldType = boundType;
    const bool oldHas = hasBand;
    bindBand();
    if (hasBand)
    {
        const auto b = model.band (boundSlot);
        const auto& p = b.sets[(size_t) b.edit];
        for (int t = 0; t < eq::NUM_TYPES; ++t) typeButtons[t]->setActive (t == b.type);
        for (int k = 0; k < eq::NUM_KEYS; ++k)
        {
            keyButtons[k]->setActive (k == b.edit);
            keyButtons[k]->setLive (k != b.edit && k != eq::St && eq::modeGroup (k) == eq::modeGroup (b.edit)
                                    && eq::setIsLive (b.type, b.sets[(size_t) k]));
        }
        fc.valueText = fmt::freq (p.fc);
        gain.valueText = fmt::gain (p.gain);
        q.valueText = String (p.q, 2);
        slope.valueText = String (p.slope) + " dB/oct";
        for (Row* r : { &fc, &gain, &q, &slope }) r->slider.setAlpha (p.on ? 1.0f : 0.35f);
    }
    if (oldSlot != boundSlot || oldType != boundType || oldHas != hasBand) resized();
    repaint();
}

void BandPanel::resized()
{
    const int pad = 12, w = getWidth(), iw = w - 2 * pad;
    for (auto* c : std::initializer_list<Component*> { &onToggle, &deleteButton }) c->setVisible (hasBand);
    for (auto* bt : typeButtons) bt->setVisible (hasBand);
    for (auto* bt : keyButtons) bt->setVisible (hasBand);
    for (Row* r : { &fc, &gain, &q, &slope }) r->slider.setVisible (false);
    if (! hasBand) return;

    const auto b = model.band (boundSlot);
    int y = 10;
    headRow = { pad, y, iw, 16 };
    deleteButton.setBounds (pad + iw - 104, y, 104, 16);
    onToggle.setBounds (pad + iw - 104 - 12 - 62, y, 62, 16);
    y += 28;

    // Tipo y canal en la misma fila (si no caben, el canal baja)
    typeLabel = { pad, y, 34, 15 };
    int x = pad + 38;
    for (auto* bt : typeButtons)
    {
        const int bw = bt->getButtonText().length() * 6 + 12;
        bt->setBounds (x, y, bw, 15);
        x += bw + 3;
    }
    int keysW = 0;
    for (auto* bt : keyButtons) keysW += bt->getButtonText().length() * 6 + 12 + 3;
    keysW += 4;
    int kx = x + 20;
    if (kx + 44 + keysW > pad + iw) { y += 22; kx = pad; }
    keyLabel = { kx, y, 42, 15 };
    kx += 46;
    for (int k = 0; k < eq::NUM_KEYS; ++k)
    {
        auto* bt = keyButtons[k];
        const int bw = bt->getButtonText().length() * 6 + 12;
        if (k == 3) kx += 4; // separación entre L/R y M/S
        bt->setBounds (kx, y, bw, 15);
        kx += bw + 3;
    }
    y += 30;

    // Deslizadores en fila
    fc.visible = true;
    gain.visible = eq::typeHasGain (b.type);
    q.visible = eq::typeHasQ (b.type);
    slope.visible = eq::typeHasSlope (b.type);
    std::vector<Row*> shown;
    for (Row* r : { &fc, &gain, &q, &slope }) { if (r->visible) shown.push_back (r); else r->label = {}; }
    const int gap = 20, cols = 3;
    const int colW = (iw - gap * (cols - 1)) / cols;
    for (size_t i = 0; i < shown.size(); ++i) shown[i]->place (pad + (int) i * (colW + gap), y, colW);
}

void BandPanel::paint (Graphics& g)
{
    if (! hasBand)
    {
        paintPanelFrame (g, getLocalBounds(), theme::muted);
        g.setColour (theme::muted.withAlpha (0.8f));
        g.setFont (theme::font (10.0f));
        g.drawFittedText (u8 ("Ninguna banda seleccionada.\nDoble clic en la gráfica para crear un punto del tipo elegido arriba; "
                              "clic derecho para elegir el tipo en el momento."),
                          getLocalBounds().reduced (16), Justification::centred, 3, 1.0f);
        return;
    }
    const auto b = model.band (boundSlot);
    const Colour col = ui::bandColour (boundSlot);
    paintPanelFrame (g, getLocalBounds(), col);
    g.setColour (col);
    g.fillEllipse ((float) headRow.getX(), (float) headRow.getCentreY() - 5, 10, 10);
    g.setColour (theme::text);
    g.setFont (theme::font (10.0f));
    g.drawText ("Banda " + String (model.number (boundSlot)) + u8 (" · ") + ui::TYPE_LABEL[b.type],
                headRow.withTrimmedLeft (18).withTrimmedRight (190), Justification::centredLeft, true);
    g.setColour (theme::muted);
    g.setFont (theme::font (9.0f));
    g.drawText ("TIPO", typeLabel, Justification::centredLeft, false);
    g.drawText ("CANAL", keyLabel, Justification::centredLeft, false);

    const float alpha = b.sets[(size_t) b.edit].on ? 1.0f : 0.35f;
    for (const Row* row : { &fc, &gain, &q, &slope })
    {
        if (! row->visible || row->label.isEmpty()) continue;
        g.setFont (theme::font (10.0f));
        g.setColour (theme::muted.withMultipliedAlpha (alpha));
        g.drawText (row->name, row->label, Justification::centredLeft, true);
        g.setColour (col.withMultipliedAlpha (alpha));
        g.drawText (row->valueText, row->label, Justification::centredRight, true);
    }
}

//==============================================================================
OutputPanel::OutputPanel (ExcellecuProcessor& p, ui::BandModel& m, std::function<void()> cb)
    : proc (p), model (m), onChange (std::move (cb))
{
    for (auto* b : { &outLr, &outMs }) addAndMakeVisible (b);
    outLr.onClick = [this] { model.set ("out_mode", 0.0f); onChange(); };
    outMs.onClick = [this] { model.set ("out_mode", 1.0f); onChange(); };
    outLr.setTooltip (u8 ("Ganancia de salida de L y R"));
    outMs.setTooltip (u8 ("Ganancia de salida de Mid y Side. Las dos parejas actúan siempre; el modo elige cuál se edita"));
    outLink.getProperties().set ("accent", (int64) theme::accent.getARGB());
    outLink.setTooltip (u8 ("Mover una ganancia mueve la otra lo mismo"));
    addAndMakeVisible (outLink);
    outLinkAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "out_link", outLink);

    int which = 0;
    for (OutRow* r : { &outA, &outB })
    {
        r->which = which++;
        r->setup (*this, "");
        auto& sl = r->slider;
        sl.setRange (-24.0, 12.0, 0.1);
        sl.setDoubleClickReturnValue (true, 0.0);
        sl.getProperties().set ("thumb", (int64) theme::accent.getARGB());
        sl.onDragStart = [this] { dragging = true; gestures (true); };
        sl.onDragEnd = [this] { gestures (false); dragging = false; onChange(); };
        sl.onValueChange = [this, r] { valueChanged (*r); };
    }
    refresh();
}

int OutputPanel::outIndex (int which) const
{
    const bool ms = proc.apvts.getRawParameterValue ("out_mode")->load() > 0.5f;
    return (ms ? 2 : 0) + which;
}

RangedAudioParameter* OutputPanel::outParam (int index) const
{
    const char* idsOut[] = { "out_l", "out_r", "out_m", "out_s" };
    return proc.apvts.getParameter (idsOut[index]);
}

// Abre o cierra el gesto de las dos ganancias de la pareja visible (un solo paso de deshacer)
void OutputPanel::gestures (bool begin)
{
    for (int w = 0; w < 2; ++w)
    {
        auto* p = outParam (outIndex (w));
        if (begin) { start[outIndex (w)] = p->convertFrom0to1 (p->getValue()); p->beginChangeGesture(); }
        else p->endChangeGesture();
    }
}

void OutputPanel::valueChanged (OutRow& r)
{
    const bool wasDragging = dragging;
    if (! wasDragging) gestures (true);   // doble clic (vuelve a 0 dB) o teclado
    const int me = outIndex (r.which), other = outIndex (1 - r.which);
    const float v = (float) r.slider.getValue();
    auto* pm = outParam (me);
    pm->setValueNotifyingHost (pm->convertTo0to1 (v));
    if (proc.apvts.getRawParameterValue ("out_link")->load() > 0.5f)
    {
        auto* po = outParam (other);
        const float ov = jlimit (-24.0f, 12.0f, std::round ((start[other] + v - start[me]) * 10.0f) / 10.0f);
        po->setValueNotifyingHost (po->convertTo0to1 (ov));
    }
    if (! wasDragging) { gestures (false); onChange(); }
    refresh();
}

void OutputPanel::refresh()
{
    auto db = [this] (int i) { return proc.apvts.getRawParameterValue (i == 0 ? "out_l" : i == 1 ? "out_r" : i == 2 ? "out_m" : "out_s")->load(); };
    const bool ms = outIndex (0) == 2;
    outLr.setActive (! ms);
    outMs.setActive (ms);
    outA.name = ms ? "Mid" : "L";
    outB.name = ms ? "Side" : "R";
    for (OutRow* r : { &outA, &outB })
    {
        const float v = db (outIndex (r->which));
        r->valueText = fmt::gain (v);
        if (! dragging) r->slider.setValue (v, dontSendNotification);
    }
    // La otra pareja también suena: se avisa si no está a 0 dB
    const int o = ms ? 0 : 2;
    const float a = db (o), b = db (o + 1);
    String n;
    if (a != 0.0f || b != 0.0f)
        n = (ms ? u8 ("También activa en L/R: L ") : u8 ("También activa en M/S: Mid "))
            + fmt::gain (a) + (ms ? u8 (" · R ") : u8 (" · Side ")) + fmt::gain (b);
    outLr.setLive (ms && (a != 0.0f || b != 0.0f));
    outMs.setLive (! ms && (a != 0.0f || b != 0.0f));
    noteText = n;
    repaint();
}

void OutputPanel::resized()
{
    const int pad = 12, w = getWidth(), iw = w - 2 * pad;
    int y = 10;
    title = { pad, y, 60, 16 };
    outLr.setBounds (pad + 62, y, 34, 15);
    outMs.setBounds (pad + 99, y, 34, 15);
    outLink.setBounds (pad + iw - 70, y, 70, 16);
    y += 28;
    outA.place (pad, y, iw);
    y += 36;
    outB.place (pad, y, iw);
    y += 36;
    note = { pad, y, iw, getHeight() - y - 6 };
}

void OutputPanel::paint (Graphics& g)
{
    paintPanelFrame (g, getLocalBounds(), theme::accent);
    g.setColour (theme::text);
    g.setFont (theme::font (10.0f));
    g.drawText ("Salida", title, Justification::centredLeft, false);
    for (const OutRow* row : { &outA, &outB })
    {
        g.setFont (theme::font (10.0f));
        g.setColour (theme::muted);
        g.drawText (row->name, row->label, Justification::centredLeft, true);
        g.setColour (row->slider.getValue() != 0.0 ? theme::accent : theme::muted);
        g.drawText (row->valueText, row->label, Justification::centredRight, true);
    }
    if (noteText.isNotEmpty())
    {
        g.setColour (theme::warn.withAlpha (0.85f));
        g.setFont (theme::font (9.0f));
        g.drawFittedText (noteText, note, Justification::topLeft, 2, 1.0f);
    }
}

//==============================================================================
ExcellecuEditor::ExcellecuEditor (ExcellecuProcessor& p)
    : AudioProcessorEditor (&p), proc (p), model (p),
      chips (model, [this] { refreshNow(); }),
      bandPanel (p, model, [this] { refreshNow(); }),
      outPanel (p, model, [this] { refreshNow(); }),
      mag (model, [this] { refreshNow(); }),
      presetBar (p, [this] { refreshNow(); }),
      meters (p)
{
    setLookAndFeel (&lnf);
    for (auto* c : std::initializer_list<Component*> { &chips, &bandPanel, &outPanel, &mag, &presetBar, &meters }) addAndMakeVisible (c);
    mag.setData (&chart);

    for (int t = 0; t < eq::NUM_TYPES; ++t)
    {
        auto* b = newTypeButtons.add (new MsButton (ui::TYPE_SHORT[t]));
        b->setTooltip (u8 ("Doble clic en la gráfica crea: ") + ui::TYPE_LABEL[t]);
        b->onClick = [this, t] { proc.newBandType = t; refreshNow(); };
        addAndMakeVisible (b);
    }
    for (int z : MagChart::ZOOMS)
    {
        auto* b = zoomButtons.add (new MsButton (u8 ("±") + String (z)));
        b->setTooltip (u8 ("Escala vertical: ±") + String (z) + " dB");
        b->onClick = [this, z] { mag.setZoomDb (z); refreshNow(); };
        addAndMakeVisible (b);
    }

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (960, 760, 2400, 1800);
    setSize (1120, 1000);
    constructed = true;
    refreshNow();
    startTimerHz (30);
}

ExcellecuEditor::~ExcellecuEditor() { setLookAndFeel (nullptr); }

void ExcellecuEditor::refreshNow()
{
    // Al construir la ventana, conectar los controles de la banda seleccionada dispara avisos
    // que llaman aquí antes de que existan todos los componentes: se ignoran hasta el final.
    if (! constructed) return;
    if (proc.selected >= 0 && ! model.used (proc.selected)) proc.selected = -1;
    for (int t = 0; t < eq::NUM_TYPES; ++t) newTypeButtons[t]->setActive (t == proc.newBandType);
    for (int i = 0; i < zoomButtons.size(); ++i) zoomButtons[i]->setActive (MagChart::ZOOMS[i] == mag.zoomDb());
    bandPanel.refresh();
    outPanel.refresh();
    presetBar.refresh();

    // Los chips pueden ocupar más o menos filas: si cambia su alto, se recoloca la gráfica
    const int ch = chips.layoutHeight (jmax (100, magPanel.getWidth() - 20));
    if (ch != shownChipsH) { shownChipsH = ch; layoutMagHeader(); }
    chips.repaint();

    const auto prm = proc.currentParams();
    const double sr = proc.sampleRateForUi();
    const int w = mag.getWidth();
    const bool changed = firstChart || ! prm.sameBands (shownParams) || prm.n != shownParams.n
                      || sr != shownRate || w != shownWidth;
    if (changed && w > 0)
    {
        firstChart = false;
        shownParams = prm; shownRate = sr; shownWidth = w;
        chart.compute (prm, sr, w);
        const int lat = eq::latencyFor (prm.n);
        const bool phaseOk = chart.maxErr < 0.01;
        status = u8 ("latencia ") + String (lat) + " muestras (" + String (1000.0 * lat / sr, 1) + u8 (" ms) · ")
               + u8 ("filtro de ") + String (prm.n + 1) + u8 (", particiones de ") + String (eq::PART)
               + u8 (" · ") + String (sr / 1000.0, 1) + u8 (" kHz · ")
               + (phaseOk ? u8 ("fase lineal ✓") : u8 ("desplazamiento de fase ") + String (chart.maxErr, 3) + u8 ("°"));
        repaint();
    }
    if (changed || shownSelected != proc.selected)
    {
        shownSelected = proc.selected;
        mag.repaint();
    }
}

void ExcellecuEditor::timerCallback() { refreshNow(); }

bool ExcellecuEditor::keyPressed (const KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown()) presetBar.redo(); else presetBar.undo();
        return true;
    }
#if ! JUCE_MAC
    if (mods.isCommandDown() && (code == 'Y' || code == 'y')) { presetBar.redo(); return true; }
#endif
    if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && proc.selected >= 0)
    {
        model.remove (proc.selected);
        refreshNow();
        return true;
    }
    return false;
}

void ExcellecuEditor::paint (Graphics& g)
{
    g.fillAll (theme::bg);

    // Título (h1)
    g.setFont (theme::font (14.0f));
    float x = 24.0f;
    auto piece = [&] (const String& s, Colour col) {
        g.setColour (col);
        g.drawText (s, Rectangle<float> (x, 18.0f, 600.0f, 20.0f), Justification::centredLeft, false);
        x += GlyphArrangement::getStringWidth (g.getCurrentFont(), s);
    };
    piece (u8 ("EQ — MOTOR "), theme::muted);
    piece ("DFT/IDFT", theme::accent);
    piece (u8 (" — PROTOTIPO V") + String (EX_VERSION_STRING), theme::muted);

    // Tarjeta del módulo
    auto r = moduleArea.toFloat().reduced (0.5f);
    g.setColour (theme::surface);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (r, 6.0f, 0.6f);
    auto header = moduleArea.withHeight (38);
    g.setColour (Colours::white.withAlpha (0.02f));
    g.fillRect (header.reduced (1, 1));
    g.setColour (theme::border);
    g.fillRect ((float) header.getX(), (float) header.getBottom(), (float) header.getWidth(), 0.6f);
    g.setFont (theme::font (9.0f));
    auto pill = Rectangle<float> ((float) header.getX() + 16, header.getCentreY() - 8.0f, 74.0f, 16.0f);
    g.setColour (theme::accent);
    g.drawRoundedRectangle (pill, 3.0f, 0.6f);
    g.drawText (u8 ("MÓDULO 2"), pill, Justification::centred, false);
    g.setFont (theme::font (11.0f));
    g.setColour (theme::text);
    g.drawText (u8 ("ECUALIZADOR — BANDA PARAMÉTRICA GAUSSIANA"), header.withTrimmedLeft (104), Justification::centredLeft, false);
    g.setFont (theme::font (10.0f));
    g.setColour (chart.maxErr < 0.01 ? theme::accent2 : theme::warn);
    g.drawText (status, header.withTrimmedRight (16), Justification::centredRight, false);

    // Marcos de la gráfica y de los medidores
    for (auto pr : { magPanel, meterPanel })
    {
        g.setColour (Colours::white.withAlpha (0.02f));
        g.fillRoundedRectangle (pr.toFloat().reduced (0.5f), 4.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (pr.toFloat().reduced (0.5f), 4.0f, 0.6f);
    }
    auto magTitle = magPanel.reduced (10).withHeight (16);
    panelTitle (g, magTitle, "MAGNITUD");
    panelTitle (g, meterPanel.reduced (10).withHeight (16), "NIVEL");
    g.setColour (theme::muted);
    g.setFont (theme::font (9.0f));
    g.drawText ("ESCALA", zoomLabel, Justification::centredRight, false);
    g.drawText ("NUEVO PUNTO", Rectangle<int> (newTypeButtons[0]->getX() - 80, magTitle.getY(), 76, 16), Justification::centredRight, false);

    g.setColour (theme::muted.withAlpha (0.7f));
    g.setFont (theme::font (9.0f));
    g.drawFittedText (u8 ("Doble clic = crear punto · clic derecho = elegir tipo / eliminar · arrastrar = frecuencia ↔ / ganancia ↕ · "
                          "Mayús ↕ = Q / pendiente · Alt (⌥) = fino · rueda = Q / pendiente · Supr = eliminar · ⌘Z / Ctrl+Z = deshacer"),
                      helpArea, Justification::topLeft, 2, 1.0f);
}

// Cabecera de la gráfica: título, escala, tipo de punto nuevo; debajo, los chips de bandas
void ExcellecuEditor::layoutMagHeader()
{
    auto mi = magPanel.reduced (10);
    auto titleRow = mi.removeFromTop (16);
    int x = titleRow.getRight();
    for (int t = eq::NUM_TYPES; --t >= 0;)
    {
        auto* b = newTypeButtons[t];
        const int bw = b->getButtonText().length() * 6 + 12;
        x -= bw;
        b->setBounds (x, titleRow.getY(), bw, 15);
        x -= 3;
    }
    int zx = titleRow.getX() + 90 + 50;
    zoomLabel = { titleRow.getX() + 90, titleRow.getY(), 46, 16 };
    for (auto* b : zoomButtons)
    {
        const int bw = b->getButtonText().length() * 6 + 10;
        b->setBounds (zx, titleRow.getY(), bw, 15);
        zx += bw + 3;
    }
    mi.removeFromTop (10);
    const int ch = chips.layoutHeight (mi.getWidth());
    shownChipsH = ch;
    chips.setBounds (mi.removeFromTop (ch));
    mi.removeFromTop (8);
    helpArea = mi.removeFromBottom (26).withTrimmedTop (6);
    mag.setBounds (mi);
    repaint();
}

void ExcellecuEditor::resized()
{
    moduleArea = getLocalBounds().reduced (24).withTrimmedTop (36);
    auto body = moduleArea.withTrimmedTop (39).reduced (16);
    presetBar.setBounds (getWidth() - 24 - PresetBar::idealWidth, 14, PresetBar::idealWidth, 28);

    meterPanel = body.removeFromRight (104);
    body.removeFromRight (12);
    meters.setBounds (meterPanel.reduced (8).withTrimmedTop (28));

    // Debajo de la gráfica: la banda y la salida, una al lado de la otra
    auto strip = body.removeFromBottom (140);
    body.removeFromBottom (12);
    outArea = strip.removeFromRight (jmax (260, (int) (strip.getWidth() * 0.3)));
    strip.removeFromRight (12);
    bandArea = strip;
    bandPanel.setBounds (bandArea);
    outPanel.setBounds (outArea);

    magPanel = body;
    layoutMagHeader();
    refreshNow();
}
