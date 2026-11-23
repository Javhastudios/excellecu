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
ControlsPanel::ControlsPanel (ExcellecuProcessor& p, ui::BandModel& m, std::function<void()> cb)
    : proc (p), model (m), onChange (cb), chips (m, cb)
{
    addAndMakeVisible (chips);
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

    // Motor: Exacto (convolución con zero-padding) o Clásico (STFT original)
    auto setEngine = [this] (int e) {
        if (auto* prm = proc.apvts.getParameter (ids::engine))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) e));
            prm->endChangeGesture();
        }
        onChange();
    };
    exactButton.setTooltip (u8 ("Convolución exacta con zero-padding: sin aliasing temporal ni huella de bloque"));
    classicButton.setTooltip (u8 ("STFT original (Hann + overlap-add), con ventana periódica"));
    exactButton.onClick = [setEngine] { setEngine (0); };
    classicButton.onClick = [setEngine] { setEngine (1); };
    addAndMakeVisible (exactButton);
    addAndMakeVisible (classicButton);

    setupRow (fc, "Frecuencia");
    setupRow (gain, "Ganancia");
    setupRow (q, "Factor Q");
    setupRow (slope, "Pendiente");
    setupRow (win, u8 ("Tamaño ventana"));
    setupRow (ov, "Overlap");
    win.paramId = ids::winSize;
    ov.paramId = ids::overlap;
    win.att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids::winSize, win.slider);
    ov.att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids::overlap, ov.slider);
    for (Row* r : { &fc, &gain, &q, &slope })
        r->slider.onDragStart = [this] { if (proc.selected >= 0) model.markEdited (proc.selected); };
    refresh();
}

void ControlsPanel::setupRow (Row& r, const String& name)
{
    r.name = name;
    r.slider.setSliderStyle (Slider::LinearHorizontal);
    r.slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    r.slider.setDoubleClickReturnValue (false, 0.0);
    addAndMakeVisible (r.slider);
}

void ControlsPanel::bindBand()
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

void ControlsPanel::refresh()
{
    const int oldSlot = boundSlot, oldType = boundType;
    const bool oldHas = hasBand;
    bindBand();
    const auto prm = model.params();

    bool needLayout = oldSlot != boundSlot || oldType != boundType || oldHas != hasBand;
    const String ct = String (model.count()) + " / " + String (eq::MAX_BANDS);
    if (ct != countText) { countText = ct; needLayout = true; }

    if (hasBand)
    {
        const auto& b = prm.bands[(size_t) boundSlot];
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
    win.valueText = proc.apvts.getParameter (ids::winSize)->getCurrentValueAsText();
    ov.valueText = proc.apvts.getParameter (ids::overlap)->getCurrentValueAsText();
    exactButton.setActive (prm.exact);
    classicButton.setActive (! prm.exact);
    ov.slider.setEnabled (! prm.exact);
    ov.slider.setAlpha (prm.exact ? 0.35f : 1.0f);
    const String note = prm.exact
        ? u8 ("Exacto: bloques de ") + String (prm.n / 2) + u8 (" muestras sin ventana, FFT de ") + String (2 * prm.n)
              + u8 (", filtro de ") + String (prm.n + 1) + u8 (" muestras. El overlap no se usa. Latencia: ") + String (prm.n) + u8 (" muestras.")
        : u8 ("Clásico: STFT con Hann periódica en análisis y síntesis. Usa 75 % de overlap para que las ventanas sumen plano. Latencia: ")
              + String (prm.n) + u8 (" muestras.");
    if (note != engineNoteText) { engineNoteText = note; needLayout = true; }

    if (needLayout) resized();
    repaint();
}

void ControlsPanel::layoutRow (Row& r, int x, int& y, int w, int gapAfter)
{
    r.label = { x, y, w, 14 };
    r.slider.setBounds (x - 6, y + 16, w + 12, 14);
    r.slider.setVisible (r.visible);
    y += 16 + 14 + gapAfter;
}

void ControlsPanel::resized()
{
    const int w = getWidth();
    int y = 0;
    bandsTitle = { 0, y, w, 16 };
    y += 26;
    const int ch = chips.layoutHeight (w);
    chips.setBounds (0, y, w, ch);
    y += ch + 10;

    const auto b = hasBand ? model.band (boundSlot) : eq::Band();
    emptyHint = {};
    bandBox = {};
    for (auto* c : std::initializer_list<Component*> { &onToggle, &deleteButton }) c->setVisible (hasBand);
    for (auto* bt : typeButtons) bt->setVisible (hasBand);
    for (auto* bt : keyButtons) bt->setVisible (hasBand);
    for (Row* r : { &fc, &gain, &q, &slope }) r->slider.setVisible (false);

    if (! hasBand)
    {
        emptyHint = { 2, y, w - 4, 52 };
        y += 52 + 4;
    }
    else
    {
        const int top = y, pad = 10, iw = w - 2 * pad;
        y += pad;
        headRow = { pad, y, iw, 16 };
        onToggle.setBounds (pad + iw - 62, y, 62, 16);
        y += 26;

        // Tipo (con salto de línea si no cabe)
        typeLabel = { pad, y, 34, 15 };
        int x = pad + 38;
        for (auto* bt : typeButtons)
        {
            const int bw = bt->getButtonText().length() * 6 + 12;
            if (x + bw > pad + iw) { x = pad + 38; y += 18; }
            bt->setBounds (x, y, bw, 15);
            x += bw + 3;
        }
        y += 25;

        // Canal
        keyLabel = { pad, y, 40, 15 };
        x = pad + iw;
        for (int k = eq::NUM_KEYS; --k >= 0;)
        {
            auto* bt = keyButtons[k];
            const int bw = bt->getButtonText().length() * 6 + 12;
            x -= bw;
            if (k == 2) x -= 4; // separación entre L/R y M/S
            bt->setBounds (x, y, bw, 15);
            x -= 3;
        }
        y += 25;

        fc.visible = true;
        gain.visible = eq::typeHasGain (b.type);
        q.visible = eq::typeHasQ (b.type);
        slope.visible = eq::typeHasSlope (b.type);
        for (Row* r : { &fc, &gain, &q, &slope })
            if (r->visible) layoutRow (*r, pad, y, iw, 12);
            else r->label = {};
        deleteButton.setBounds (pad, y, iw, 18);
        y += 18 + pad;
        bandBox = { 0, top, w, y - top };
        y += 12;
    }

    dividers[0] = { 0, y, w, 1 };
    y += 13;
    motorTitle = { 0, y, w, 16 };
    y += 26;
    engineLabel = { 0, y, 40, 15 };
    classicButton.setBounds (w - 54, y, 54, 15);
    exactButton.setBounds (w - 54 - 3 - 48, y, 48, 15);
    y += 25;
    win.visible = ov.visible = true;
    layoutRow (win, 0, y, w, 12);
    layoutRow (ov, 0, y, w, 6);
    engineNote = { 0, y, w, 48 };
    y += 48;
    contentHeight = y + 8;
    if (getHeight() != contentHeight) setSize (w, contentHeight);
}

void ControlsPanel::paint (Graphics& g)
{
    panelTitle (g, bandsTitle, "BANDAS");
    g.setColour (theme::muted);
    g.drawText (countText, bandsTitle, Justification::centredRight, false);

    if (! emptyHint.isEmpty())
    {
        g.setColour (theme::muted.withAlpha (0.8f));
        g.setFont (theme::font (10.0f));
        g.drawFittedText (u8 ("Doble clic en la gráfica para crear un punto del tipo elegido arriba.\n"
                              "Clic derecho en la gráfica para elegir el tipo en el momento."),
                          emptyHint, Justification::topLeft, 4, 1.0f);
    }

    if (hasBand)
    {
        const auto b = model.band (boundSlot);
        const Colour col = ui::bandColour (boundSlot);
        auto r = bandBox.toFloat().reduced (0.5f);
        g.setColour (col.withAlpha (0x0d / 255.0f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col.withAlpha (0x55 / 255.0f));
        g.drawRoundedRectangle (r, 4.0f, 0.6f);

        g.setColour (col);
        g.fillEllipse ((float) headRow.getX(), (float) headRow.getCentreY() - 5, 10, 10);
        g.setColour (theme::text);
        g.setFont (theme::font (10.0f));
        g.drawText ("Banda " + String (model.number (boundSlot)) + u8 (" · ") + ui::TYPE_LABEL[b.type],
                    headRow.withTrimmedLeft (18).withTrimmedRight (66), Justification::centredLeft, true);
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

    g.setColour (theme::border);
    g.fillRect ((float) dividers[0].getX(), (float) dividers[0].getY(), (float) dividers[0].getWidth(), 0.6f);
    panelTitle (g, motorTitle, "MOTOR DFT");
    g.setColour (theme::muted);
    g.setFont (theme::font (9.0f));
    g.drawText ("MODO", engineLabel, Justification::centredLeft, false);
    g.setColour (theme::muted.withAlpha (0.8f));
    g.drawFittedText (engineNoteText, engineNote, Justification::topLeft, 5, 1.0f);
    for (const Row* row : { &win, &ov })
    {
        const float a = (row == &ov && ! ov.slider.isEnabled()) ? 0.35f : 1.0f;
        g.setFont (theme::font (10.0f));
        g.setColour (theme::muted.withMultipliedAlpha (a));
        g.drawText (row->name, row->label, Justification::centredLeft, true);
        g.setColour (theme::accent2.withMultipliedAlpha (a));
        g.drawText (row->valueText, row->label, Justification::centredRight, true);
    }
}

//==============================================================================
ExcellecuEditor::ExcellecuEditor (ExcellecuProcessor& p)
    : AudioProcessorEditor (&p), proc (p), model (p),
      controls (p, model, [this] { refreshNow(); }),
      mag (model, [this] { refreshNow(); }),
      phase (model)
{
    setLookAndFeel (&lnf);
    viewport.setViewedComponent (&controls, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
    addAndMakeVisible (mag);
    addAndMakeVisible (phase);
    mag.setData (&chart);
    phase.setData (&chart);

    for (int t = 0; t < eq::NUM_TYPES; ++t)
    {
        auto* b = newTypeButtons.add (new MsButton (ui::TYPE_SHORT[t]));
        b->setTooltip (u8 ("Doble clic en la gráfica crea: ") + ui::TYPE_LABEL[t]);
        b->onClick = [this, t] { proc.newBandType = t; refreshNow(); };
        addAndMakeVisible (b);
    }

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (900, 620, 2000, 1600);
    setSize (1100, 780);
    refreshNow();
    startTimerHz (30);
}

ExcellecuEditor::~ExcellecuEditor() { setLookAndFeel (nullptr); }

void ExcellecuEditor::refreshNow()
{
    if (proc.selected >= 0 && ! model.used (proc.selected)) proc.selected = -1;
    for (int t = 0; t < eq::NUM_TYPES; ++t) newTypeButtons[t]->setActive (t == proc.newBandType);
    controls.refresh();

    const auto prm = proc.currentParams();
    const double sr = proc.sampleRateForUi();
    const int w = mag.getWidth();
    const bool changed = firstChart || ! prm.sameBands (shownParams) || prm.n != shownParams.n
                      || prm.overlap != shownParams.overlap || prm.exact != shownParams.exact || sr != shownRate || w != shownWidth;
    if (changed && w > 0)
    {
        firstChart = false;
        shownParams = prm; shownRate = sr; shownWidth = w;
        chart.compute (prm, sr, w);
        const int hop = (int) std::floor (prm.n * (1 - prm.overlap));
        status = u8 ("latencia ") + String (prm.n) + " muestras (" + String (1000.0 * prm.n / sr, 1) + u8 (" ms) · ")
               + (prm.exact ? u8 ("exacto, bloque ") + String (prm.n / 2) : u8 ("clásico, hop ") + String (hop))
               + u8 (" · ") + String (sr / 1000.0, 1) + " kHz";
        repaint();
    }
    if (changed || shownSelected != proc.selected)
    {
        shownSelected = proc.selected;
        mag.repaint();
        phase.repaint();
    }
}

void ExcellecuEditor::timerCallback() { refreshNow(); }

bool ExcellecuEditor::keyPressed (const KeyPress& k)
{
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
    piece (u8 (" — PROTOTIPO V0.18"), theme::muted);

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
    g.setColour (theme::accent2);
    g.drawText (status, header.withTrimmedRight (16), Justification::centredRight, false);

    // Marcos de las gráficas
    for (auto pr : { magPanel, phasePanel })
    {
        g.setColour (Colours::white.withAlpha (0.02f));
        g.fillRoundedRectangle (pr.toFloat().reduced (0.5f), 4.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (pr.toFloat().reduced (0.5f), 4.0f, 0.6f);
    }
    auto magTitle = magPanel.reduced (10).withHeight (16);
    panelTitle (g, magTitle, "MAGNITUD");
    g.setColour (theme::muted);
    g.setFont (theme::font (9.0f));
    g.drawText ("NUEVO PUNTO", Rectangle<int> (newTypeButtons[0]->getX() - 80, magTitle.getY(), 76, 16), Justification::centredRight, false);

    auto phTitle = phasePanel.reduced (10).withHeight (16);
    panelTitle (g, phTitle, "FASE");
    {
        const bool ok = chart.maxErr < 0.01;
        const String pre = chart.multiRole ? u8 ("L·R·M·S · ") : String();
        const String t = pre + (ok ? u8 ("sin desplazamiento ✓") : u8 ("desplazamiento: ") + String (chart.maxErr, 3) + u8 ("°"));
        const Colour c = ok ? theme::accent2 : theme::warn;
        g.setFont (theme::font (9.0f));
        const float w = GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 12;
        auto badge = Rectangle<float> ((float) phTitle.getX() + 40, (float) phTitle.getY(), w, 16.0f);
        g.setColour (c.withAlpha (0.12f)); g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (c.withAlpha (0.3f)); g.drawRoundedRectangle (badge, 3.0f, 0.6f);
        g.setColour (c); g.drawText (t, badge, Justification::centred, false);
    }

    g.setColour (theme::muted.withAlpha (0.7f));
    g.setFont (theme::font (9.0f));
    g.drawFittedText (u8 ("Doble clic = crear punto · clic derecho = elegir tipo / eliminar · arrastrar = frecuencia ↔ / ganancia ↕ · "
                          "Mayús ↕ = Q / pendiente · Alt (⌥) = fino · rueda = Q / pendiente · Supr = eliminar"),
                      helpArea, Justification::topLeft, 2, 1.0f);
}

void ExcellecuEditor::resized()
{
    moduleArea = getLocalBounds().reduced (24).withTrimmedTop (36);
    auto body = moduleArea.withTrimmedTop (39).reduced (16);
    auto left = body.removeFromLeft (panelW + 8);
    body.removeFromLeft (8);
    viewport.setBounds (left);
    controls.setSize (panelW, controls.idealHeight());
    controls.resized();

    const int magH = (int) (body.getHeight() * 0.62);
    magPanel = body.removeFromTop (magH);
    body.removeFromTop (12);
    phasePanel = body;

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
    mi.removeFromTop (10);
    helpArea = mi.removeFromBottom (26).withTrimmedTop (6);
    mag.setBounds (mi);

    auto pi = phasePanel.reduced (10);
    pi.removeFromTop (26);
    phase.setBounds (pi);
    refreshNow();
}
