#include "Views.h"
#include "Format.h"
#include "Theme.h"

using namespace juce;

namespace
{
String u8 (const char* t) { return String::fromUTF8 (t); }
constexpr float HANDLE_R = 6, HANDLE_HIT = 11;
constexpr float Q_DRAG_PX_PER_X4 = 100, SLOPE_DRAG_PX = 25;

int slopeIndex (int slope)
{
    for (int i = 0; i < 4; ++i) if (eq::SLOPES[(size_t) i] == slope) return i;
    return 1;
}

void freqGrid (Graphics& g, float l, float r, float t, float bottom, float W)
{
    const double lMin = std::log10 (ui::MAG_F_MIN), lMax = std::log10 (ui::MAG_F_MAX);
    const float pw = W - l - r;
    for (int f : { 20, 100, 500, 1000, 5000, 10000, 20000 })
    {
        const float x = l + (float) ((std::log10 ((double) f) - lMin) / (lMax - lMin)) * pw;
        g.setColour (Colours::white.withAlpha (0.05f));
        g.drawLine (x, t, x, bottom, 0.5f);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.setFont (theme::font (9.0f));
        g.drawText (f >= 1000 ? String (f / 1000) + "k" : String (f), Rectangle<float> (x - 20, bottom + 4, 40, 12), Justification::centred, false);
    }
}

Path handlePath (int type, float x, float y, float r)
{
    Path p;
    if (eq::typeHasSlope (type)) p.addRoundedRectangle (x - r, y - r, 2 * r, 2 * r, 2.0f);
    else if (type == eq::LowShelf || type == eq::HighShelf)
    {
        const float d = r * 1.3f;
        p.startNewSubPath (x, y - d); p.lineTo (x + d, y); p.lineTo (x, y + d); p.lineTo (x - d, y); p.closeSubPath();
    }
    else p.addEllipse (x - r, y - r, 2 * r, 2 * r);
    return p;
}

void strokeDashed (Graphics& g, const Path& path, float width, float a, float b)
{
    Path dashed;
    const float dash[] = { a, b };
    PathStrokeType (width, PathStrokeType::curved).createDashedStroke (dashed, path, dash, 2);
    g.fillPath (dashed);
}
} // namespace

//==============================================================================
void ChartData::compute (const eq::Params& params, double sampleRate, int plotWidth)
{
    p = params;
    sr = sampleRate;
    width = plotWidth;
    const int N = p.n, h = N / 2;
    const bool hasLR = p.anyGroup (eq::GroupLR), hasMS = p.anyGroup (eq::GroupMS);
    multiRole = hasLR || hasMS;

    std::array<eq::Response, 4> R;
    std::vector<double> freqs ((size_t) h);
    for (int role = 0; role < 4; ++role)
    {
        R[(size_t) role].g.assign ((size_t) h, 1.0);
        eq::computeResponse (p, role, N, sr, R[(size_t) role], freqs);
    }

    // Curvas de diseño, un punto por píxel
    const int n = std::max (200, plotWidth);
    plotF.resize ((size_t) n);
    const double lMin = std::log10 (ui::MAG_F_MIN), lMax = std::log10 (ui::MAG_F_MAX);
    for (int i = 0; i < n; ++i) plotF[(size_t) i] = std::pow (10.0, lMin + (lMax - lMin) * i / (n - 1));
    std::array<std::vector<double>, 4> P;
    for (int role = 0; role < 4; ++role)
    {
        P[(size_t) role].resize ((size_t) n);
        eq::responseAt (p, role, plotF.data(), n, P[(size_t) role].data());
    }
    auto toDb = [] (const std::vector<double>& m) {
        std::vector<double> d (m.size());
        for (size_t k = 0; k < m.size(); ++k) d[k] = m[k] > 1e-10 ? 20 * std::log10 (m[k]) : -120;
        return d;
    };
    curves.clear();
    if (! hasLR && ! hasMS) curves.push_back ({ toDb (P[eq::RoleL]), Colour (0xffd9d9e6), {}, false });
    if (hasLR)
    {
        curves.push_back ({ toDb (P[eq::RoleL]), Colour (0xfff2d24b), "L", false });
        curves.push_back ({ toDb (P[eq::RoleR]), Colour (0xffff7aa8), "R", true });
    }
    if (hasMS)
    {
        auto withBase = [&] (const std::vector<double>& m) {
            if (hasLR) return m;
            std::vector<double> o (m.size());
            for (size_t k = 0; k < m.size(); ++k) o[k] = m[k] * P[eq::RoleL][k];
            return o;
        };
        curves.push_back ({ toDb (withBase (P[eq::RoleM])), Colour (0xfff7a23e), hasLR ? "Mid (M/S)" : "Mid", false });
        curves.push_back ({ toDb (withBase (P[eq::RoleS])), Colour (0xff5bc0eb), hasLR ? "Side (M/S)" : "Side", true });
    }
    anyActive = false;
    for (auto& c : curves) for (double v : c.db) if (std::abs (v) > 0.001) { anyActive = true; break; }

    // Fase: se comprueba en los cuatro roles; se dibuja el primero con alguna banda activa
    int phaseRole = 0;
    for (int role = 0; role < 4; ++role) if (! R[(size_t) role].identity) { phaseRole = role; break; }

    std::vector<double> re0 ((size_t) N), im0 ((size_t) N, 0.0);
    for (int i = 0; i < N; ++i) re0[(size_t) i] = std::sin (2 * eq::PI * i * 7 / N) + std::sin (2 * eq::PI * i * 13 / N);
    eq::Fft fft (N);
    fft.forward (re0.data(), im0.data());
    phaseBefore.assign ((size_t) h, 0.0);
    for (int k = 0; k < h; ++k) phaseBefore[(size_t) k] = std::atan2 (im0[(size_t) k], re0[(size_t) k]) * 180 / eq::PI;

    const double binHz = sr / N;
    maxErr = 0;
    for (int role = 0; role < 4; ++role)
    {
        auto re = re0, im = im0;
        eq::applyResponse (re.data(), im.data(), N, R[(size_t) role]);
        std::vector<double> ph ((size_t) h);
        for (int k = 0; k < h; ++k) ph[(size_t) k] = std::atan2 (im[(size_t) k], re[(size_t) k]) * 180 / eq::PI;
        for (int k = 1; k < h; ++k)
        {
            if (std::abs (R[(size_t) role].g[(size_t) k]) < 0.001) continue;
            const double f = k * binHz;
            if (f < 20 || f > 20000) continue;
            double diff = std::abs (ph[(size_t) k] - phaseBefore[(size_t) k]);
            if (diff > 180) diff = 360 - diff;
            maxErr = std::max (maxErr, diff);
        }
        if (role == phaseRole)
        {
            phaseAfter = ph;
            phaseMag.resize ((size_t) h);
            for (int k = 0; k < h; ++k) phaseMag[(size_t) k] = std::abs (R[(size_t) role].g[(size_t) k]);
        }
    }
}

//==============================================================================
float MagChart::Geom::toX (double f) const
{
    const double lMin = std::log10 (ui::MAG_F_MIN), lMax = std::log10 (ui::MAG_F_MAX);
    return f > 0 ? l + (float) ((std::log10 (f) - lMin) / (lMax - lMin)) * pw : l;
}
double MagChart::Geom::fromX (float x) const
{
    const double lMin = std::log10 (ui::MAG_F_MIN), lMax = std::log10 (ui::MAG_F_MAX);
    return std::pow (10.0, lMin + (x - l) / pw * (lMax - lMin));
}
float MagChart::Geom::toY (double db) const
{
    if (! std::isfinite (db)) db = db < 0 ? lo : hi;
    return t + ph - (float) ((jlimit (lo, hi, db) - lo) / (hi - lo)) * ph;
}
double MagChart::Geom::fromY (float y) const { return lo + (t + ph - y) / ph * (hi - lo); }

int MagChart::zoomDb() const
{
    const int z = model.proc.apvts.state.getProperty ("zoomDb", 12);
    for (int v : ZOOMS) if (v == z) return z;
    return 12;
}

void MagChart::setZoomDb (int db)
{
    model.proc.apvts.state.setProperty ("zoomDb", db, nullptr);
    repaint();
}

MagChart::Geom MagChart::geom() const
{
    Geom g;
    g.W = (float) getWidth(); g.H = (float) getHeight();
    g.pw = g.W - g.l - g.r; g.ph = g.H - g.t - g.b;
    const double z = zoomDb();
    g.hi = z * 1.1;
    g.lo = -z * 1.1;
    return g;
}

MagChart::MagChart (ui::BandModel& m, std::function<void()> cb) : model (m), onChange (std::move (cb))
{
    setWantsKeyboardFocus (false);
}

// Puntos visibles: de las bandas no seleccionadas solo el juego editado; de la seleccionada
// también su hermano en L/R o M/S. La seleccionada va encima.
std::vector<MagChart::Handle> MagChart::handles() const
{
    std::vector<Handle> out;
    const auto prm = model.params();
    const auto G = geom();
    const int sel = model.proc.selected;
    std::vector<int> order;
    for (int s = 0; s < eq::MAX_BANDS; ++s) if (prm.bands[(size_t) s].used && s != sel) order.push_back (s);
    if (sel >= 0 && prm.bands[(size_t) sel].used) order.push_back (sel);
    for (int s : order)
    {
        const auto& b = prm.bands[(size_t) s];
        const auto g = eq::modeGroup (b.edit);
        std::vector<int> keys { b.edit };
        if (s == sel && g != eq::GroupSt)
            keys.insert (keys.begin(), g == eq::GroupLR ? (b.edit == eq::L ? eq::R : eq::L) : (b.edit == eq::M ? eq::S : eq::M));
        for (int k : keys)
        {
            const auto& p = b.sets[(size_t) k];
            out.push_back ({ s, k, p, b.type, G.toX (p.fc), G.toY (eq::typeHasGain (b.type) ? p.gain : 0.0), k == b.edit, s == sel });
        }
    }
    return out;
}

MagChart::Hit MagChart::hitAt (Point<float> pos) const
{
    auto hs = handles();
    for (auto it = hs.rbegin(); it != hs.rend(); ++it)
        if (std::hypot (it->x - pos.x, it->y - pos.y) <= HANDLE_HIT) return { it->slot, it->key };
    return {};
}

void MagChart::paint (Graphics& g)
{
    if (data == nullptr) return;
    const auto& d = *data;
    const auto G = geom();

    const int z = zoomDb();
    const int stepDb = z == 3 ? 1 : z == 6 ? 2 : z == 12 ? 3 : 6;
    for (int db = -z; db <= z; db += stepDb)
    {
        const float y = G.toY (db);
        g.setColour (Colours::white.withAlpha (db == 0 ? 0.15f : 0.05f));
        g.drawLine (G.l, y, G.W - G.r, y, db == 0 ? 1.0f : 0.5f);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.setFont (theme::font (9.0f));
        g.drawText ((db > 0 ? "+" : "") + String (db) + "dB", Rectangle<float> (0, y - 6, G.l - 3, 12), Justification::centredRight, false);
    }
    freqGrid (g, G.l, G.r, G.t, G.H - G.b, G.W);
    const float zY = G.toY (0);

    if (d.anyActive)
    {
        for (size_t ci = 0; ci < d.curves.size(); ++ci)
        {
            const auto& c = d.curves[ci];
            Path fill, line;
            bool s = false;
            for (size_t k = 0; k < c.db.size(); ++k)
            {
                const double f = d.plotF[k];
                if (f < ui::MAG_F_MIN || f > ui::MAG_F_MAX || ! std::isfinite (c.db[k])) continue;
                const float x = G.toX (f), y = G.toY (c.db[k]);
                if (! s) { fill.startNewSubPath (x, zY); fill.lineTo (x, y); line.startNewSubPath (x, y); s = true; }
                else { fill.lineTo (x, y); line.lineTo (x, y); }
            }
            fill.lineTo (G.toX (ui::MAG_F_MAX), zY);
            fill.closeSubPath();
            g.setColour (c.colour.withAlpha (d.curves.size() > 1 ? 0.08f : 0.15f));
            g.fillPath (fill);
            g.setColour (c.colour);
            if (c.dashed) strokeDashed (g, line, 2.0f, 4, 3);
            else g.strokePath (line, PathStrokeType (2.0f, PathStrokeType::curved));

            if (c.label.isNotEmpty())
            {
                const float step = 70, lx = G.W - G.r - (float) d.curves.size() * step + (float) ci * step, ly = G.t + 8;
                Path seg; seg.startNewSubPath (lx, ly); seg.lineTo (lx + 14, ly);
                if (c.dashed) strokeDashed (g, seg, 1.5f, 4, 3);
                else g.strokePath (seg, PathStrokeType (1.5f));
                g.setFont (theme::font (9.0f));
                g.drawText (c.label, Rectangle<float> (lx + 18, ly - 6, step - 18, 12), Justification::centredLeft, false);
            }
        }
    }
    else
    {
        g.setColour (Colours::white.withAlpha (0.15f));
        g.drawLine (G.l, zY, G.W - G.r, zY, 1.0f);
        g.setFont (theme::font (9.0f));
        g.drawText (model.count() > 0 ? u8 ("sin efecto") : u8 ("doble clic para crear un punto · clic derecho para elegir el tipo"),
                    Rectangle<float> (G.l, zY + 6, G.pw, 12), Justification::centred, false);
    }

    // Banda seleccionada: su curva propia (punteada) y marcador de frecuencia
    const int sel = model.proc.selected;
    if (sel >= 0 && d.p.bands[(size_t) sel].used)
    {
        const auto& b = d.p.bands[(size_t) sel];
        const auto& sp = b.sets[(size_t) b.edit];
        const Colour col = ui::bandColour (sel);
        const bool live = eq::setIsLive (b.type, sp);
        Path line;
        bool started = false;
        for (size_t k = 0; k < d.plotF.size(); ++k)
        {
            const double f = d.plotF[k];
            if (f < ui::MAG_F_MIN || f > ui::MAG_F_MAX) continue;
            const double gk = live ? eq::bandGainAt (b.type, sp, f) : 1.0;
            const double db = gk > 1e-10 ? 20 * std::log10 (gk) : -120;
            const float x = G.toX (f), y = G.toY (db);
            if (! started) { line.startNewSubPath (x, y); started = true; } else line.lineTo (x, y);
        }
        g.setColour (col.withAlpha (0.7f));
        strokeDashed (g, line, 1.2f, 2, 2);
        const float fcX = G.toX (sp.fc);
        const float dash[] = { 3.0f, 3.0f };
        g.setColour (col.withAlpha (0.35f));
        g.drawDashedLine (Line<float> (fcX, G.t, fcX, G.H - G.b), dash, 2, 0.5f);
        g.setColour (col.withAlpha (0.8f));
        g.setFont (theme::font (9.0f));
        const float lx = jlimit (G.l + 22, G.W - G.r - 26, fcX);
        g.drawText (fmt::freq (sp.fc), Rectangle<float> (lx - 40, G.t + 3, 80, 12), Justification::centred, false);
    }

    // Puntos
    for (const auto& h : handles())
    {
        const Colour col = ui::bandColour (h.slot);
        const Hit me { h.slot, h.key };
        const bool hot = hover == me || (drag && drag->h == me);
        const float r = hot ? HANDLE_R + 2 : HANDLE_R;
        const float a = h.p.on ? 1.0f : 0.4f;
        if (h.selected && h.editing)
        {
            g.setColour (col.withAlpha (a * 0.5f));
            g.strokePath (handlePath (h.type, h.x, h.y, r + 4), PathStrokeType (1.0f));
        }
        const Path hp = handlePath (h.type, h.x, h.y, r);
        if (h.editing)
        {
            g.setColour (col.withAlpha (a)); g.fillPath (hp);
            g.setColour (theme::bg.withAlpha (a)); g.strokePath (hp, PathStrokeType (1.5f));
        }
        else
        {
            g.setColour (Colour (0xcc0e0e10).withMultipliedAlpha (a)); g.fillPath (hp);
            g.setColour (col.withAlpha (a * 0.6f)); g.strokePath (hp, PathStrokeType (1.5f));
        }
        const String lbl = h.key != eq::St ? String (ui::KEY_LABEL[h.key]) : String (model.number (h.slot));
        g.setColour ((h.editing ? theme::bg : col).withAlpha (a));
        g.setFont (theme::font (8.0f).boldened());
        g.drawText (lbl, Rectangle<float> (h.x - 10, h.y - 5, 20, 10), Justification::centred, false);

        if (hot)
        {
            String txt = String (model.number (h.slot)) + u8 (" · ") + ui::TYPE_SHORT[h.type] + u8 (" · ") + fmt::freq (h.p.fc);
            if (eq::typeHasSlope (h.type)) txt << u8 (" · ") << h.p.slope << " dB/oct";
            else txt << u8 (" · ") << fmt::gain (h.p.gain) << u8 (" · Q ") << String (h.p.q, 2);
            if (! h.p.on) txt << u8 (" · apagada");
            g.setFont (theme::font (9.0f));
            const float tw = GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 10;
            float tx = h.x - tw / 2, ty = h.y - r - 20;
            if (ty < G.t) ty = h.y + r + 6;
            tx = jlimit (G.l, std::max (G.l, G.W - G.r - tw), tx);
            g.setColour (Colour (0xeb0e0e10)); g.fillRect (tx, ty, tw, 14.0f);
            g.setColour (col); g.drawRect (tx, ty, tw, 14.0f, 0.5f);
            g.drawText (txt, Rectangle<float> (tx + 5, ty, tw - 5, 14), Justification::centredLeft, false);
        }
    }
}

//==============================================================================
void MagChart::focus (Hit h)
{
    model.proc.selected = h.slot;
    if (h.key != model.band (h.slot).edit) model.setMode (h.slot, h.key);
}

void MagChart::rebase (float x, float y, bool fine)
{
    auto& d = *drag;
    const auto b = model.band (d.h.slot);
    const auto& p = b.sets[(size_t) b.edit];
    d.x0 = x; d.y0 = y; d.fine = fine;
    d.fc0 = p.fc; d.gain0 = p.gain; d.q0 = p.q; d.slopeIdx0 = slopeIndex (p.slope);
}

void MagChart::updateModifiers (float x, float y, bool shift, bool alt)
{
    if (! drag) return;
    drag->lastX = x; drag->lastY = y;
    if (shift == drag->shape && alt == drag->fine) return;
    drag->shape = shift;
    rebase (x, y, alt);
    setMouseCursor (drag->shape ? MouseCursor::UpDownResizeCursor : MouseCursor::DraggingHandCursor);
}

void MagChart::beginDragGestures (bool begin)
{
    for (const char* f : { "fc", "gain", "q", "slope" })
    {
        const auto id = ids::set (drag->h.slot, drag->h.key, f);
        if (begin) model.begin (id); else model.end (id);
    }
}

void MagChart::mouseMove (const MouseEvent& e)
{
    const Hit h = hitAt (e.position);
    setMouseCursor (h.valid() ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
    if (! (h == hover)) { hover = h; repaint(); }
}

void MagChart::mouseExit (const MouseEvent&)
{
    if (! drag && hover.valid()) { hover = {}; setMouseCursor (MouseCursor::NormalCursor); repaint(); }
}

void MagChart::mouseDown (const MouseEvent& e)
{
    if (auto* ed = findParentComponentOfClass<AudioProcessorEditor>()) ed->grabKeyboardFocus(); // para Supr
    if (e.mods.isPopupMenu()) { showMenu (e); return; }
    const Hit h = hitAt (e.position);
    if (! h.valid())
    {
        if (model.proc.selected >= 0) { model.proc.selected = -1; changed(); }
        return;
    }
    focus (h);
    drag = Drag();
    drag->h = h;
    drag->shape = e.mods.isShiftDown();
    rebase (e.position.x, e.position.y, e.mods.isAltDown());
    beginDragGestures (true);
    setMouseCursor (drag->shape ? MouseCursor::UpDownResizeCursor : MouseCursor::DraggingHandCursor);
    changed();
}

void MagChart::mouseDrag (const MouseEvent& e)
{
    if (! drag) return;
    const float x = e.position.x, y = e.position.y;
    updateModifiers (x, y, e.mods.isShiftDown(), e.mods.isAltDown());
    auto& d = *drag;
    const auto b = model.band (d.h.slot);
    const int key = b.edit;
    const float fine = d.fine ? 0.2f : 1.0f;
    if (d.shape)
    {
        const float up = -(y - d.y0) * fine;
        if (eq::typeHasQ (b.type))
        {
            const double q = d.q0 * std::pow (4.0, up / Q_DRAG_PX_PER_X4);
            model.setLive (ids::set (d.h.slot, key, "q"), (float) (std::round (jlimit (ui::EQ_Q_MIN, ui::EQ_Q_MAX, q) * 100) / 100));
        }
        else
        {
            const int i = jlimit (0, 3, d.slopeIdx0 + (int) std::round (up / SLOPE_DRAG_PX));
            model.setLive (ids::set (d.h.slot, key, "slope"), (float) i);
        }
    }
    else
    {
        const auto G = geom();
        const double lMin = std::log10 (ui::MAG_F_MIN), lMax = std::log10 (ui::MAG_F_MAX);
        const double dLog = (x - d.x0) / G.pw * (lMax - lMin) * fine;
        model.setLive (ids::set (d.h.slot, key, "fc"), (float) jlimit (ui::MAG_F_MIN, ui::MAG_F_MAX, d.fc0 * std::pow (10.0, dLog)));
        if (eq::typeHasGain (b.type))
        {
            const double dDb = -(y - d.y0) / G.ph * (G.hi - G.lo) * fine;   // con zoom, más fino
            model.setLive (ids::set (d.h.slot, key, "gain"), (float) (std::round (jlimit (ui::EQ_GAIN_MIN, ui::EQ_GAIN_MAX, d.gain0 + dDb) * 10) / 10));
        }
    }
    model.markEdited (d.h.slot);
    changed();
}

void MagChart::mouseUp (const MouseEvent& e)
{
    if (! drag) return;
    beginDragGestures (false);
    drag.reset();
    hover = hitAt (e.position);
    setMouseCursor (hover.valid() ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
    changed();
}

void MagChart::modifierKeysChanged (const ModifierKeys& mods)
{
    if (drag) updateModifiers (drag->lastX, drag->lastY, mods.isShiftDown(), mods.isAltDown());
}

// Doble clic: en un punto con ganancia → 0 dB · en vacío → crear punto del tipo elegido
void MagChart::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const Hit h = hitAt (e.position);
    if (h.valid())
    {
        const auto b = model.band (h.slot);
        if (! eq::typeHasGain (b.type)) return;
        model.set (ids::set (h.slot, h.key, "gain"), 0.0f);
        model.markEdited (h.slot);
        changed();
        return;
    }
    const auto G = geom();
    model.create (model.proc.newBandType, G.fromX (e.position.x), G.fromY (e.position.y));
    changed();
}

// Rueda sobre un punto: Q en campana/shelf, pendiente en los filtros
void MagChart::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const Hit h = drag ? drag->h : hitAt (e.position);
    if (! h.valid()) { Component::mouseWheelMove (e, w); return; }
    focus (h);
    const auto b = model.band (h.slot);
    const auto& p = b.sets[(size_t) b.edit];
    if (eq::typeHasQ (b.type))
    {
        if (w.deltaY == 0) return;
        const double factor = std::pow (1.06, e.mods.isAltDown() ? 0.25 : 1.0);
        const double q = w.deltaY > 0 ? p.q * factor : p.q / factor;
        model.set (ids::set (h.slot, b.edit, "q"), (float) (std::round (jlimit (ui::EQ_Q_MIN, ui::EQ_Q_MAX, q) * 100) / 100));
    }
    else
    {
        wheelAccum += w.deltaY;
        if (std::abs (wheelAccum) < 0.05f) return;
        const int i = slopeIndex (p.slope);
        const int ni = jlimit (0, 3, i + (wheelAccum > 0 ? 1 : -1));
        wheelAccum = 0;
        if (ni == i) return;
        model.set (ids::set (h.slot, b.edit, "slope"), (float) ni);
    }
    hover = h;
    model.markEdited (h.slot);
    changed();
}

// Clic derecho: crear con un tipo, o cambiar tipo / apagar / eliminar
void MagChart::showMenu (const MouseEvent& e)
{
    const Hit h = hitAt (e.position);
    PopupMenu m;
    SafePointer<MagChart> safe (this);
    if (h.valid())
    {
        focus (h);
        changed();
        const auto b = model.band (h.slot);
        const int slot = h.slot;
        m.addSectionHeader ("Banda " + String (model.number (slot)) + u8 (" · tipo"));
        for (int t = 0; t < eq::NUM_TYPES; ++t)
            m.addItem (ui::TYPE_LABEL[t], true, b.type == t, [safe, slot, t] { if (safe) { safe->model.setType (slot, t); safe->changed(); } });
        m.addSeparator();
        const bool on = b.sets[(size_t) b.edit].on;
        m.addItem (on ? "Apagar" : "Encender", [safe, slot, on] {
            if (! safe) return;
            const auto bb = safe->model.band (slot);
            safe->model.set (ids::set (slot, bb.edit, "on"), on ? 0.0f : 1.0f);
            safe->model.markEdited (slot);
            safe->changed();
        });
        m.addItem ("Eliminar  (Supr)", [safe, slot] { if (safe) { safe->model.remove (slot); safe->hover = {}; safe->changed(); } });
    }
    else
    {
        const bool full = model.count() >= eq::MAX_BANDS;
        const auto G = geom();
        const double fc = jlimit (ui::MAG_F_MIN, ui::MAG_F_MAX, G.fromX (e.position.x));
        const double gain = G.fromY (e.position.y);
        m.addSectionHeader (full ? u8 ("Máximo 12 bandas") : "Crear en " + fmt::freq (fc));
        if (! full)
            for (int t = 0; t < eq::NUM_TYPES; ++t)
                m.addItem (ui::TYPE_LABEL[t], true, model.proc.newBandType == t,
                           [safe, t, fc, gain] { if (safe) { safe->model.create (t, fc, gain); safe->changed(); } });
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

//==============================================================================
void PhaseView::paint (Graphics& g)
{
    if (data == nullptr) return;
    const auto& d = *data;
    const float W = (float) getWidth(), H = (float) getHeight();
    const float l = 40, r = 8, t = 8, b = 24, pw = W - l - r, ph = H - t - b;
    const double fMin = std::log10 (20.0), fMax = std::log10 (20000.0);
    const double binHz = d.sr / d.p.n;

    for (int deg : { -180, -90, 0, 90, 180 })
    {
        const float y = t + ph / 2 - (float) deg / 180.0f * (ph / 2);
        g.setColour (Colours::white.withAlpha (deg == 0 ? 0.15f : 0.05f));
        g.drawLine (l, y, W - r, y, deg == 0 ? 1.0f : 0.5f);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.setFont (theme::font (9.0f));
        g.drawText (String (deg) + u8 ("°"), Rectangle<float> (0, y - 6, l - 3, 12), Justification::centredRight, false);
    }
    freqGrid (g, l, r, t, H - b, W);

    auto toY = [&] (double deg) { return t + ph - (float) ((jlimit (-180.0, 180.0, deg) + 180) / 360) * ph; };
    auto toX = [&] (double f) { return l + (float) ((std::log10 (f) - fMin) / (fMax - fMin)) * pw; };

    // Como mucho un punto por píxel: con N = 8192 hay 4096 bins, casi todos apretados en los
    // agudos y saltando entre ±180°; dibujarlos todos (y además a trazos) bloqueaba la interfaz.
    Path before, after;
    bool s1 = false, s2 = false;
    const size_t bins = d.phaseBefore.size();
    size_t lastK = 0;
    const int cols = std::max (1, (int) pw);
    for (int px = 0; px <= cols; ++px)
    {
        const double f = std::pow (10.0, fMin + (fMax - fMin) * px / cols);
        const size_t k = (size_t) std::lround (f / binHz);
        if (k < 1 || k >= bins || k == lastK) continue;
        lastK = k;
        const float x = toX ((double) k * binHz);
        if (! s1) { before.startNewSubPath (x, toY (d.phaseBefore[k])); s1 = true; } else before.lineTo (x, toY (d.phaseBefore[k]));
        if (d.phaseMag[k] < 0.001) continue;
        if (! s2) { after.startNewSubPath (x, toY (d.phaseAfter[k])); s2 = true; } else after.lineTo (x, toY (d.phaseAfter[k]));
    }
    g.setColour (theme::accent);
    g.strokePath (before, PathStrokeType (1.5f, PathStrokeType::mitered));
    g.setColour (theme::warn);
    strokeDashed (g, after, 1.5f, 4, 3);

    g.setFont (theme::font (9.0f));
    g.setColour (theme::accent);
    g.drawLine (l + 8, t + 8, l + 22, t + 8, 1.5f);
    g.drawText ("antes EQ", Rectangle<float> (l + 26, t + 2, 70, 12), Justification::centredLeft, false);
    g.setColour (theme::warn);
    const float dash[] = { 4.0f, 3.0f };
    g.drawDashedLine (Line<float> (l + 90, t + 8, l + 104, t + 8), dash, 2, 1.5f);
    g.drawText (u8 ("después EQ"), Rectangle<float> (l + 108, t + 2, 80, 12), Justification::centredLeft, false);

    const int sel = model.proc.selected;
    if (sel >= 0 && d.p.bands[(size_t) sel].used)
    {
        const auto& bd = d.p.bands[(size_t) sel];
        const double fc = bd.sets[(size_t) bd.edit].fc;
        const Colour col = ui::bandColour (sel);
        const float x = toX (fc);
        const float dd[] = { 3.0f, 3.0f };
        g.setColour (col.withAlpha (0.35f));
        g.drawDashedLine (Line<float> (x, t, x, H - b), dd, 2, 0.5f);
        g.setColour (col.withAlpha (0.8f));
        g.drawText (fmt::freq (fc), Rectangle<float> (x - 40, t + 3, 80, 12), Justification::centred, false);
    }
}
