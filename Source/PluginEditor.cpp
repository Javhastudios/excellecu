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
        const int w = 6 + 7 + 4 + text.length() * 7 + 8;
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
    // Forma, resonancia y codo: cada botón fija su parámetro de la banda seleccionada
    auto choiceButtons = [this] (juce::OwnedArray<MsButton>& arr, const char* field, std::initializer_list<const char*> names,
                                 std::initializer_list<const char*> tips) {
        auto tip = tips.begin();
        int v = 0;
        for (const char* n : names)
        {
            auto* b = arr.add (new MsButton (String::fromUTF8 (n)));
            b->setTooltip (String::fromUTF8 (*tip++));
            b->onClick = [this, field, v] {
                if (proc.selected >= 0) { model.set (ids::band (proc.selected, field), (float) v); onChange(); }
            };
            addAndMakeVisible (b);
            ++v;
        }
    };
    choiceButtons (shapeButtons, "shape", { "TDM", "Classic" },
                   { "Curva TDM: al subir más ancha, al bajar más estrecha",
                     "Curva analógica clásica, simétrica: bajar es el espejo de subir" });
    choiceButtons (resButtons, "res", { "Las dos", "Base", "Cénit" },
                   { "Shelf analógico tal cual: resonancia en la base y en el cénit (con Q por encima de 0,71)",
                     "Solo la resonancia de la base (lado sin tocar), como el truco del Pultec",
                     "Solo la resonancia del cénit (lado realzado o atenuado): empujón cerca del corte sin el valle del otro lado" });
    choiceButtons (kneeButtons, "knee", { "Recorte", "Suave", "Más suave" },
                   { "La curva llega a la línea y se queda plana de golpe",
                     "Codo redondeado de 1/6 de octava donde la curva toca la línea",
                     "Ese lado sin resonancia, con la misma pendiente en fc: llega a la línea poco a poco" });
    typeGroup = { "TIPO", {}, &typeButtons };
    keyGroup = { "CANAL", {}, &keyButtons, true, 3 };   // separación entre L/R y M/S
    shapeGroup = { "FORMA", {}, &shapeButtons };
    resGroup = { "RESONANCIA", {}, &resButtons };
    kneeGroup = { "CODO", {}, &kneeButtons };
    addChildComponent (entry);

    deleteButton.setDanger (true);
    deleteButton.onClick = [this] { if (proc.selected >= 0) { model.remove (proc.selected); onChange(); } };
    addAndMakeVisible (deleteButton);
    soloButton.setTooltip (u8 ("Escuchar solo lo que hace esta banda, en su canal (sigue a la banda seleccionada)"));
    soloButton.onClick = [this] { proc.soloOn = ! proc.soloOn; onChange(); };
    addAndMakeVisible (soloButton);
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
        soloButton.setActive (proc.soloOn);
        for (int k = 0; k < eq::NUM_KEYS; ++k)
        {
            keyButtons[k]->setActive (k == b.edit);
            keyButtons[k]->setLive (k != b.edit && k != eq::St && eq::modeGroup (k) == eq::modeGroup (b.edit)
                                    && eq::setIsLive (b.type, b.sets[(size_t) k]));
        }
        for (int i = 0; i < shapeButtons.size(); ++i) shapeButtons[i]->setActive (i == b.shape);
        for (int i = 0; i < resButtons.size(); ++i) resButtons[i]->setActive (i == b.res);
        for (int i = 0; i < kneeButtons.size(); ++i) kneeButtons[i]->setActive (i == b.knee);
        const bool classicShelf = b.shape == eq::ShapeClassic && (b.type == eq::LowShelf || b.type == eq::HighShelf);
        const bool shownNow[] = { eq::typeHasGain (b.type), classicShelf, classicShelf && b.res != eq::ResBoth };
        Group* opt[] = { &shapeGroup, &resGroup, &kneeGroup };
        bool relayout = false;
        for (int i = 0; i < 3; ++i) if (opt[i]->shown != shownNow[i]) { opt[i]->shown = shownNow[i]; relayout = true; }
        // Sin resonancias con Q ≤ 0,71: resonancia y codo atenuados (como la web)
        const float resAlpha = p.q <= std::sqrt (0.5) ? 0.45f : 1.0f;
        for (auto* bt : resButtons) bt->setAlpha (resAlpha);
        for (auto* bt : kneeButtons) bt->setAlpha (resAlpha);
        if (relayout) resized();
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
    for (auto* c : std::initializer_list<Component*> { &onToggle, &deleteButton, &soloButton }) c->setVisible (hasBand);
    for (auto* bt : typeButtons) bt->setVisible (hasBand);
    for (auto* bt : keyButtons) bt->setVisible (hasBand);
    for (Group* gr : { &shapeGroup, &resGroup, &kneeGroup })
        for (auto* bt : *gr->buttons) bt->setVisible (hasBand && gr->shown);
    for (Row* r : { &fc, &gain, &q, &slope }) r->slider.setVisible (false);
    if (! hasBand) return;

    const auto b = model.band (boundSlot);
    int y = 10;
    headRow = { pad, y, iw, 16 };
    deleteButton.setBounds (pad + iw - 104, y, 104, 16);
    soloButton.setBounds (pad + iw - 104 - 10 - 70, y, 70, 16);
    onToggle.setBounds (pad + iw - 104 - 10 - 70 - 12 - 62, y, 62, 16);
    y += 28;

    // Tipo, canal, forma, resonancia y codo seguidos; si no caben, bajan de fila
    y = flow ({ &typeGroup, &keyGroup, &shapeGroup, &resGroup, &kneeGroup }, pad, iw, y, true) + 30;

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

static int buttonWidth (const String& text) { return text.length() * 7 + 12; }

int BandPanel::flow (const std::vector<Group*>& groups, int pad, int width, int y, bool place)
{
    int x = pad;
    for (auto* gr : groups)
    {
        if (! gr->shown) { gr->label = {}; continue; }
        const int labelW = gr->title.length() * 7 + 2;
        int w = labelW + 4;
        for (int i = 0; i < gr->buttons->size(); ++i)
            w += buttonWidth ((*gr->buttons)[i]->getButtonText()) + 3 + (i == gr->gapAfter ? 4 : 0);
        if (x > pad && x + w > pad + width) { y += 22; x = pad; }
        if (place)
        {
            gr->label = { x, y, labelW, 15 };
            int bx = x + labelW + 4;
            for (int i = 0; i < gr->buttons->size(); ++i)
            {
                auto* bt = (*gr->buttons)[i];
                if (i == gr->gapAfter) bx += 4;
                const int bw = buttonWidth (bt->getButtonText());
                bt->setBounds (bx, y, bw, 15);
                bx += bw + 3;
            }
        }
        x += w + 20;
    }
    return y;
}

int BandPanel::preferredHeight (int width)
{
    // Mismos grupos que el panel, todos a la vista
    auto make = [] (std::initializer_list<const char*> names) {
        auto arr = std::make_unique<OwnedArray<MsButton>>();
        for (const char* n : names) arr->add (new MsButton (String::fromUTF8 (n)));
        return arr;
    };
    auto t = make ({ "Campana", "L-Shelf", "H-Shelf", "HP", "LP" }), k = make ({ "Estéreo", "L", "R", "M", "S" });
    auto sh = make ({ "TDM", "Classic" }), rs = make ({ "Las dos", "Base", "Cénit" }), kn = make ({ "Recorte", "Suave", "Más suave" });
    Group gt { "TIPO", {}, t.get() }, gk { "CANAL", {}, k.get(), true, 3 }, gs { "FORMA", {}, sh.get() },
          gr { "RESONANCIA", {}, rs.get() }, gn { "CODO", {}, kn.get() };
    const int pad = 12;
    const int y = flow ({ &gt, &gk, &gs, &gr, &gn }, pad, width - 2 * pad, 38, false) + 30;
    return y + 30 + 12;
}

// Doble clic en la cifra de un deslizador: escribir el valor
void BandPanel::mouseDoubleClick (const MouseEvent& e)
{
    if (! hasBand) return;
    for (Row* r : { &fc, &gain, &q, &slope })
    {
        if (! r->visible || r->label.isEmpty()) continue;
        const auto valueArea = r->label.withTrimmedLeft (r->label.getWidth() / 2);
        if (! valueArea.contains (e.getPosition())) continue;
        const int slot = boundSlot;
        const auto b = model.band (slot);
        const auto& p = b.sets[(size_t) b.edit];
        String current;
        if (r == &fc) current = p.fc >= 1000 ? String (p.fc / 1000, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "k"
                                             : String (p.fc, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        else if (r == &gain) current = String (p.gain, 1);
        else if (r == &q) current = String (p.q, 2);
        else current = String (p.slope);
        const String id = r->paramId;
        Row* row = r;
        entry.start (valueArea.withSizeKeepingCentre (valueArea.getWidth(), 18).translated (0, -2), current,
                     [this, slot, id, row] (const String& text) {
            double v = fmt::parseNumber (text);
            if (! std::isfinite (v) || slot != boundSlot) return;
            if (row == &fc) v = jlimit (ui::MAG_F_MIN, ui::MAG_F_MAX, v);
            else if (row == &gain) v = std::round (jlimit (ui::EQ_GAIN_MIN, ui::EQ_GAIN_MAX, v) * 10) / 10;
            else if (row == &q) v = std::round (jlimit (ui::EQ_Q_MIN, ui::EQ_Q_MAX, v) * 100) / 100;
            else
            {
                int best = 0;
                for (int i = 1; i < 4; ++i) if (std::abs (eq::SLOPES[(size_t) i] - v) < std::abs (eq::SLOPES[(size_t) best] - v)) best = i;
                v = best;   // el parámetro de pendiente es el índice
            }
            model.markEdited (slot);
            model.set (id, (float) v);
            onChange();
        });
        return;
    }
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
                headRow.withTrimmedLeft (18).withTrimmedRight (270), Justification::centredLeft, true);
    g.setColour (theme::muted);
    g.setFont (theme::font (9.0f));
    for (const Group* gr : { &typeGroup, &keyGroup, &shapeGroup, &resGroup, &kneeGroup })
        if (gr->shown && ! gr->label.isEmpty()) g.drawText (gr->title, gr->label, Justification::centredLeft, false);

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
    addChildComponent (entry);

    int which = 0;
    for (OutRow* r : { &outA, &outB })
    {
        r->which = which++;
        r->setup (*this, "");
        auto& sl = r->slider;
        sl.setRange (-24.0, 12.0, 0.1);
        sl.setDoubleClickReturnValue (true, 0.0);
        sl.getProperties().set ("thumb", (int64) theme::accent.getARGB());
        sl.onDragStart = [this, r] { dragging = true; dragRow = r; gestures (true); };
        sl.onDragEnd = [this] { gestures (false); dragging = false; dragRow = nullptr; onChange(); };
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

// Doble clic en la cifra: escribir la ganancia; con «Enlazar», la otra se mueve lo mismo
void OutputPanel::mouseDoubleClick (const MouseEvent& e)
{
    for (OutRow* r : { &outA, &outB })
    {
        const auto valueArea = r->label.withTrimmedLeft (r->label.getWidth() / 2);
        if (! valueArea.contains (e.getPosition())) continue;
        const int which = r->which;
        entry.start (valueArea.withSizeKeepingCentre (valueArea.getWidth(), 18).translated (0, -2),
                     String (outParam (outIndex (which))->convertFrom0to1 (outParam (outIndex (which))->getValue()), 1),
                     [this, which] (const String& text) {
            const double v = fmt::parseNumber (text);
            if (! std::isfinite (v)) return;
            const int me = outIndex (which), other = outIndex (1 - which);
            auto* pm = outParam (me);
            auto* po = outParam (other);
            const float old = pm->convertFrom0to1 (pm->getValue());
            const float nv = jlimit (-24.0f, 12.0f, std::round ((float) v * 10.0f) / 10.0f);
            const bool link = proc.apvts.getRawParameterValue ("out_link")->load() > 0.5f;
            pm->beginChangeGesture();
            if (link) po->beginChangeGesture();
            pm->setValueNotifyingHost (pm->convertTo0to1 (nv));
            if (link)
            {
                const float ov = jlimit (-24.0f, 12.0f, std::round ((po->convertFrom0to1 (po->getValue()) + nv - old) * 10.0f) / 10.0f);
                po->setValueNotifyingHost (po->convertTo0to1 (ov));
                po->endChangeGesture();
            }
            pm->endChangeGesture();
            onChange();
        });
        return;
    }
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
        // Solo se deja quieto el que se está arrastrando: el otro sigue al parámetro en tiempo real
        if (! dragging || r != dragRow) r->slider.setValue (v, dontSendNotification);
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

    // Analizador: Pre y Post se encienden y apagan; velocidad e inclinación cambian con cada clic
    {
        const char* names[] = { "Pre", "Post", "Media", "0 dB/oct" };
        const char* tips[] = { "Espectro de la entrada (antes del EQ), en gris",
                               "Espectro de lo que suena (después del EQ y la salida)",
                               "Caída del analizador (clic para cambiar)",
                               "Inclinación del analizador alrededor de 1 kHz (clic para cambiar). En 0, el ruido rosa sale plano" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = anaButtons.add (new MsButton (String::fromUTF8 (names[i])));
            b->setTooltip (String::fromUTF8 (tips[i]));
            b->onClick = [this, i] {
                auto& a = mag.analyzer;
                mag.setAnalyzer (i == 0 ? ! a.showPre : a.showPre, i == 1 ? ! a.showPost : a.showPost,
                                 i == 2 ? (a.speed + 1) % 3 : a.speed, i == 3 ? (a.tilt + 1) % 4 : a.tilt);
                syncAnaButtons();
            };
            addAndMakeVisible (b);
        }
        syncAnaButtons();
    }

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (960, 760, 2400, 1800);
    setSize (1120, 1000);
    constructed = true;
    refreshNow();
    startTimerHz (30);
}

void ExcellecuEditor::syncAnaButtons()
{
    const auto& a = mag.analyzer;
    anaButtons[0]->setActive (a.showPre);
    anaButtons[1]->setActive (a.showPost);
    anaButtons[2]->setButtonText (String::fromUTF8 (ui::Analyzer::SPEED_NAMES[a.speed]));
    anaButtons[3]->setButtonText (String (ui::Analyzer::TILTS[a.tilt], 1).replace (".0", "").replace (".", ",") + " dB/oct");
}

ExcellecuEditor::~ExcellecuEditor()
{
    // Escuchar banda solo vive mientras se ve la ventana
    proc.soloOn = false;
    proc.setSolo (-1, eq::St);
    setLookAndFeel (nullptr);
}

void ExcellecuEditor::refreshNow()
{
    // Al construir la ventana, conectar los controles de la banda seleccionada dispara avisos
    // que llaman aquí antes de que existan todos los componentes: se ignoran hasta el final.
    if (! constructed) return;
    if (proc.selected >= 0 && ! model.used (proc.selected)) proc.selected = -1;
    // Escuchar banda: sigue a la banda seleccionada y al juego que se edita; sin banda, se apaga
    if (proc.soloOn && proc.selected < 0) proc.soloOn = false;
    if (proc.soloOn) proc.setSolo (proc.selected, model.band (proc.selected).edit);
    else proc.setSolo (-1, eq::St);
    for (int t = 0; t < eq::NUM_TYPES; ++t) newTypeButtons[t]->setActive (t == proc.newBandType);
    for (int i = 0; i < zoomButtons.size(); ++i) zoomButtons[i]->setActive (MagChart::ZOOMS[i] == mag.zoomDb());
    bandPanel.refresh();
    outPanel.refresh();
    presetBar.refresh();

    // Los chips pueden ocupar más o menos filas: si cambia su alto, se recoloca la gráfica
    const int ch = chips.layoutHeight (chipsW);
    if (ch != shownChipsH) { shownChipsH = ch; layoutMagHeader(); }
    chips.repaint();

    auto prm = proc.currentParams();
    prm.soloSlot = -1;   // la gráfica enseña la curva del EQ también mientras se escucha una banda
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
    g.drawText ("ANALIZADOR", anaLabel, Justification::centredRight, false);
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
        const int bw = b->getButtonText().length() * 7 + 12;
        x -= bw;
        b->setBounds (x, titleRow.getY(), bw, 15);
        x -= 3;
    }
    int zx = titleRow.getX() + 90 + 50;
    zoomLabel = { titleRow.getX() + 90, titleRow.getY(), 46, 16 };
    for (auto* b : zoomButtons)
    {
        const int bw = b->getButtonText().length() * 7 + 10;
        b->setBounds (zx, titleRow.getY(), bw, 15);
        zx += bw + 3;
    }
    mi.removeFromTop (10);
    // Analizador: en la fila del título si cabe entre la escala y «Nuevo punto» (como la web); si
    // no, a la derecha de la fila de los chips
    const int labelW = 74, gap = 3;
    int anaW = labelW + 4;
    for (auto* b : anaButtons) anaW += jmax (b->getButtonText().length() * 7 + 12, b == anaButtons[2] ? 60 : b == anaButtons[3] ? 74 : 0) + gap;
    const int newTypeLeft = newTypeButtons[0]->getX() - 84;
    const bool inTitle = zx + 24 + anaW <= newTypeLeft - 16;
    int ax = inTitle ? zx + 24 : mi.getRight() - anaW;
    const int ay = inTitle ? titleRow.getY() : mi.getY();
    anaLabel = { ax, ay, labelW, 16 };
    ax += labelW + 4;
    for (auto* b : anaButtons)
    {
        const int bw = jmax (b->getButtonText().length() * 7 + 12, b == anaButtons[2] ? 60 : b == anaButtons[3] ? 74 : 0);
        b->setBounds (ax, ay, bw, 15);
        ax += bw + gap;
    }
    chipsW = inTitle ? mi.getWidth() : jmax (100, mi.getWidth() - anaW - 16);
    const int ch = chips.layoutHeight (chipsW);
    shownChipsH = ch;
    chips.setBounds (mi.removeFromTop (ch).withWidth (chipsW));
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
    const int bandW = body.getWidth() - jmax (260, (int) (body.getWidth() * 0.3)) - 12;   // como bandArea, abajo
    auto strip = body.removeFromBottom (jmax (140, BandPanel::preferredHeight (bandW)));
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
