#include "Meters.h"
#include "Theme.h"

using namespace juce;

namespace
{
float toDb (float g) { return g > 1e-6f ? 20.0f * std::log10 (g) : -120.0f; }
String dbText (float db) { return db <= -99 ? String::fromUTF8 ("−∞") : (db > 0 ? "+" : "") + String (db, 1); }
}

MeterView::MeterView (ExcellecuProcessor& p) : proc (p)
{
    setTooltip (String::fromUTF8 ("Pico (claro) y RMS (oscuro). Clic: borra saturación y máximos"));
    // Con la ventana cerrada la cola se llena y deja de admitir bloques: lo que quede es viejo
    for (auto* m : { &proc.inMeter, &proc.outMeter }) for (auto& c : *m) c.collect();
    aesButton.setTooltip (String::fromUTF8 ("RMS según AES-17: +3,01 dB, un seno marca lo mismo en pico y en RMS"));
    flatButton.setTooltip (String::fromUTF8 ("RMS tal cual: un seno marca 3 dB por debajo de su pico"));
    aesButton.onClick = [this] { setAes17 (true); };
    flatButton.onClick = [this] { setAes17 (false); };
    addAndMakeVisible (aesButton);
    addAndMakeVisible (flatButton);
    setAes17 (aes17());
    startTimerHz (30);
}

bool MeterView::aes17() const { return proc.apvts.state.getProperty ("rmsAes17", true); }

void MeterView::setAes17 (bool on)
{
    proc.apvts.state.setProperty ("rmsAes17", on, nullptr);   // se guarda con la sesión
    aesButton.setActive (on);
    flatButton.setActive (! on);
    repaint();
}

void MeterView::resized()
{
    const int w = getWidth(), h = getHeight();
    aesButton.setBounds (0, h - 36, w, 15);
    flatButton.setBounds (0, h - 17, w, 15);
}

void MeterView::tick()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = lastTick > 0 ? (float) jlimit (0.0, 0.2, (now - lastTick) / 1000.0) : 0.0f;
    lastTick = now;
    channels = jlimit (1, 2, proc.meterChannels.load());
    for (int g = 0; g < 2; ++g)
        for (int c = 0; c < 2; ++c)
        {
            auto& m = (g == 0 ? proc.inMeter : proc.outMeter)[(size_t) c];
            auto& ch = groups[(size_t) g][(size_t) c];
            const auto blk = m.collect();
            const float pk = blk.peak, ss = blk.sumSq;
            const int n = blk.count;
            ch.ss[(size_t) ch.pos] = ss;
            ch.n[(size_t) ch.pos] = n;
            ch.pos = (ch.pos + 1) % (int) ch.ss.size();
            float sumS = 0; int sumN = 0;
            for (size_t i = 0; i < ch.ss.size(); ++i) { sumS += ch.ss[i]; sumN += ch.n[i]; }
            ch.rmsDb = sumN > 0 ? toDb (std::sqrt (sumS / (float) sumN)) : -120.0f;

            const float db = toDb (pk);
            ch.peakDb = jmax (db, ch.peakDb - 20.0f * dt);   // caída de 20 dB/s
            if (db >= ch.holdDb || now - ch.holdTime > 1500.0) { ch.holdDb = db; ch.holdTime = now; }
            ch.maxDb = jmax (ch.maxDb, db);
            if (pk >= 1.0f) ch.clip = true;
        }
    repaint();
}

void MeterView::mouseUp (const MouseEvent&)
{
    for (auto& g : groups)
        for (auto& ch : g) { ch.clip = false; ch.maxDb = -100; ch.holdDb = -100; }
    repaint();
}

float MeterView::yFor (float db, Rectangle<float> bar) const
{
    const float t = jlimit (0.0f, 1.0f, (db - DB_MIN) / (DB_MAX - DB_MIN));
    return bar.getBottom() - t * bar.getHeight();
}

void MeterView::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat().withTrimmedBottom (42);   // abajo, el selector de RMS
    const float titleH = 16, ledH = 6, numH = 26;
    const float scaleW = 20;
    const float groupW = (area.getWidth() - scaleW) / 2;
    const char* titles[] = { "ENT", "SAL" };

    // Zona común de las barras
    const float top = area.getY() + titleH + 4 + ledH + 3;
    const float bottom = area.getBottom() - numH;

    // Escala en el centro
    g.setFont (theme::font (8.0f));
    const float sx = area.getX() + groupW;
    const Rectangle<float> scaleBar (sx, top, scaleW, bottom - top);
    for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        const float y = yFor (db, scaleBar);
        g.setColour (db == 0 ? theme::warn.withAlpha (0.8f) : theme::muted.withAlpha (0.8f));
        g.drawText ((db > 0 ? "+" : "") + String ((int) std::abs (db)), Rectangle<float> (sx, y - 5, scaleW, 10), Justification::centred, false);
    }

    for (int gi = 0; gi < 2; ++gi)
    {
        const float gx = area.getX() + (gi == 0 ? 0 : groupW + scaleW);
        g.setColour (theme::muted);
        g.setFont (theme::font (9.0f));
        g.drawText (titles[gi], Rectangle<float> (gx, area.getY(), groupW, titleH), Justification::centred, false);

        const float gap = 3;
        const float bw = (groupW - 6 - gap * (channels - 1)) / channels;
        float maxDb = -120;
        bool anyClip = false;
        for (int c = 0; c < channels; ++c)
        {
            const auto& ch = groups[(size_t) gi][(size_t) c];
            maxDb = jmax (maxDb, ch.maxDb);
            anyClip = anyClip || ch.clip;
            const float x = gx + 3 + c * (bw + gap);
            const Rectangle<float> bar (x, top, bw, bottom - top);

            // Piloto de saturación
            const Rectangle<float> led (x, area.getY() + titleH + 4, bw, ledH);
            g.setColour (ch.clip ? Colour (0xffff4d4d) : theme::border);
            g.fillRoundedRectangle (led, 1.5f);

            g.setColour (Colours::white.withAlpha (0.03f));
            g.fillRect (bar);
            // 0 dBFS
            g.setColour (theme::warn.withAlpha (0.35f));
            g.fillRect (bar.getX(), yFor (0, bar), bar.getWidth(), 0.6f);

            auto fill = [&] (float db, Colour col) {
                if (db <= DB_MIN) return;
                const float y = yFor (db, bar);
                g.setColour (col);
                g.fillRect (Rectangle<float> (bar.getX(), y, bar.getWidth(), bar.getBottom() - y));
            };
            const Colour base = gi == 0 ? theme::accent2 : theme::accent;
            fill (ch.peakDb, (ch.peakDb > 0 ? Colour (0xffff4d4d) : base).withAlpha (0.45f));
            const float rms = shownRms (ch);
            fill (rms, (rms > 0 ? Colour (0xffff4d4d) : base).withAlpha (0.85f));
            if (ch.holdDb > DB_MIN)
            {
                g.setColour (ch.holdDb > 0 ? Colour (0xffff4d4d) : theme::text.withAlpha (0.8f));
                g.fillRect (bar.getX(), yFor (ch.holdDb, bar) - 0.5f, bar.getWidth(), 1.2f);
            }
            if (channels == 2)
            {
                g.setColour (theme::muted.withAlpha (0.7f));
                g.setFont (theme::font (8.0f));
                g.drawText (c == 0 ? "L" : "R", Rectangle<float> (x, bottom + 1, bw, 10), Justification::centred, false);
            }
        }
        // Máximo desde el último clic
        g.setColour (anyClip ? Colour (0xffff4d4d) : theme::text.withAlpha (0.85f));
        g.setFont (theme::font (9.0f));
        g.drawText (dbText (maxDb), Rectangle<float> (gx, bottom + 12, groupW, 12), Justification::centred, false);
    }
}
