#include "PluginEditor.h"

//==============================================================================
ResponseCurve::ResponseCurve (MedidoresEQAudioProcessor& p) : proc (p)
{
    shown.fill (-120.0f);
    held.fill (-120.0f);
    setTooltip ("Drag a point: frequency and gain (hold Shift for fine adjustment). Mouse wheel over a point: Q. "
                "Double-click a point: band on/off. The side handles of the focused band change its width (Q).");
    startTimerHz (30);
}

float ResponseCurve::rangeDb() const
{
    return EQ::rangeDbFor ((int) proc.apvts.getRawParameterValue (EQ::rangeId)->load());
}

float ResponseCurve::xForFreq (float f) const
{
    return (float) getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}
float ResponseCurve::freqForX (float x) const
{
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, x / (float) getWidth()));
}
float ResponseCurve::yForDb (float d) const
{
    const float r = rangeDb();
    return (float) getHeight() * (1.0f - (d + r) / (2.0f * r));
}
float ResponseCurve::dbForY (float y) const
{
    const float r = rangeDb();
    return -r + 2.0f * r * (1.0f - y / (float) getHeight());
}

bool ResponseCurve::isNotch (int b) const
{
    return ! EQ::isCut (b) && EQ::readSettings (b, proc.apvts, 48000.0).shape == EQ::NotchShape;
}

juce::Point<float> ResponseCurve::nodePos (int b) const
{
    const float f = proc.apvts.getRawParameterValue (EQ::freqId (b))->load();
    const float g = (EQ::isCut (b) || isNotch (b)) ? 0.0f : proc.apvts.getRawParameterValue (EQ::gainId (b))->load();
    return { xForFreq (f), juce::jlimit (0.0f, (float) getHeight(), yForDb (g)) };
}

int ResponseCurve::nodeAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 14.0f;
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

bool ResponseCurve::isBell (int b) const
{
    if (EQ::isCut (b)) return false;
    const auto shape = EQ::readSettings (b, proc.apvts, 48000.0).shape;
    return shape == EQ::Peak || shape == EQ::NotchShape;
}

// Ancho en octavas <-> Q (campana): BW = 2·asinh(1/(2Q))/ln2
static float octavesForQ (float q)
{
    const float x = 1.0f / (2.0f * q);
    return 2.0f * std::log2 (x + std::sqrt (x * x + 1.0f));
}
static float qForOctaves (float octaves)
{
    return 1.0f / (2.0f * std::sinh (octaves * 0.34657359f));   // ln2/2
}

float ResponseCurve::effectiveQ (int b) const
{
    const auto s = EQ::readSettings (b, proc.apvts, 48000.0);
    return s.shape == EQ::NotchShape ? s.q : EQ::styleQ (s.style, EQ::Peak, s.q, s.gainDb);
}

juce::Point<float> ResponseCurve::handlePos (int b, int side) const
{
    const float f = proc.apvts.getRawParameterValue (EQ::freqId (b))->load();
    const float oct = octavesForQ (effectiveQ (b));
    const float fe = juce::jlimit (20.0f, 20000.0f, f * std::pow (2.0f, (float) side * oct * 0.5f));
    return { xForFreq (fe), nodePos (b).y };
}

int ResponseCurve::handleAt (juce::Point<float> p) const
{
    if (focusBand < 0 || ! isBell (focusBand)
        || proc.apvts.getRawParameterValue (EQ::onId (focusBand))->load() < 0.5f)
        return 0;
    if (handlePos (focusBand, -1).getDistanceFrom (p) < 9.0f) return -1;
    if (handlePos (focusBand, +1).getDistanceFrom (p) < 9.0f) return +1;
    return 0;
}

//==============================================================================
void ResponseCurve::timerCallback()
{
    updateSpectrum();
    repaint();
}

void ResponseCurve::setupFft (int order)
{
    fftOrder = order;
    fftSize = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window = std::make_unique<juce::dsp::WindowingFunction<float>> ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann);
    ring.assign ((size_t) fftSize, 0.0f);
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    bins.assign ((size_t) fftSize / 2, -120.0f);
    prefix.assign ((size_t) fftSize / 2 + 1, 0.0f);
    shown.fill (-120.0f);
    held.fill (-120.0f);
}

void ResponseCurve::updateSpectrum()
{
    const int order = EQ::analyzerOrderFor ((int) proc.apvts.getRawParameterValue (EQ::analyzerResId)->load());
    if (order != fftOrder) setupFft (order);

    const bool holdOn = proc.apvts.getRawParameterValue (EQ::analyzerHoldId)->load() > 0.5f;
    if (holdOn != holdWasOn) { held.fill (-120.0f); holdWasOn = holdOn; }

    float tmp[4096];
    bool got = false;
    while (int n = proc.pullAnalyzerSamples (tmp, 4096))
    {
        got = true;
        if (n >= fftSize)
            std::copy (tmp + n - fftSize, tmp + n, ring.begin());
        else
        {
            std::copy (ring.begin() + n, ring.end(), ring.begin());
            std::copy (tmp, tmp + n, ring.end() - n);
        }
    }

    const int half = fftSize / 2;
    if (! got)
    {
        for (auto& s : bins) s = juce::jmax (-120.0f, s - 3.0f);
    }
    else
    {
        std::copy (ring.begin(), ring.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        window->multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft->performFrequencyOnlyForwardTransform (fftData.data());

        const int speed = (int) proc.apvts.getRawParameterValue (EQ::analyzerSpeedId)->load();
        const float w = speed == 0 ? 0.12f : (speed == 2 ? 0.6f : 0.3f);   // peso de la medida nueva
        for (int i = 0; i < half; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * 4.0f / (float) fftSize, -120.0f);
            bins[(size_t) i] = bins[(size_t) i] * (1.0f - w) + db * w;
        }
    }

    // Suma acumulada de la potencia para promediar por fracciones de octava sin coste
    prefix[0] = 0.0f;
    for (int i = 0; i < half; ++i)
        prefix[(size_t) i + 1] = prefix[(size_t) i] + std::pow (10.0f, bins[(size_t) i] * 0.1f);

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    const int smooth = (int) proc.apvts.getRawParameterValue (EQ::analyzerSmoothId)->load();
    const float octaves = smooth == 1 ? 1.0f / 6.0f : (smooth == 2 ? 1.0f / 3.0f : 0.0f);
    const float binHz = (float) sr / (float) fftSize;

    for (int j = 0; j < numPoints; ++j)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) j / (float) (numPoints - 1));
        shownFreq[(size_t) j] = f;
        float db;
        if (octaves > 0.0f)
        {
            const float lo = f * std::pow (2.0f, -octaves * 0.5f), hi = f * std::pow (2.0f, octaves * 0.5f);
            const int b0 = juce::jlimit (1, half - 1, (int) std::floor (lo / binHz));
            const int b1 = juce::jlimit (b0 + 1, half, (int) std::ceil (hi / binHz) + 1);
            const float mean = (prefix[(size_t) b1] - prefix[(size_t) b0]) / (float) (b1 - b0);
            db = 10.0f * std::log10 (juce::jmax (mean, 1.0e-12f));
        }
        else
        {
            // Sin suavizar: interpolación entre bins a graves, el máximo de los bins que caen en cada punto a agudos
            const float pos = f / binHz;
            const float next = 20.0f * std::pow (1000.0f, (float) (j + 1) / (float) (numPoints - 1)) / binHz;
            const int b0 = juce::jlimit (1, half - 2, (int) std::floor (pos));
            if (next - pos > 1.0f)
            {
                db = -120.0f;
                for (int i = b0; i <= juce::jmin (half - 1, (int) std::floor (next)); ++i) db = juce::jmax (db, bins[(size_t) i]);
            }
            else
            {
                const float frac = pos - (float) b0;
                db = bins[(size_t) b0] * (1.0f - frac) + bins[(size_t) b0 + 1] * frac;
            }
        }
        shown[(size_t) j] = db;
        if (holdOn) held[(size_t) j] = juce::jmax (held[(size_t) j], db);
    }
}

//==============================================================================
void ResponseCurve::paint (juce::Graphics& g)
{
    const auto& P = paletteOf (*this);
    const auto area = getLocalBounds().toFloat();
    const float R = rangeDb();
    g.setColour (P.screen);
    g.fillRoundedRectangle (area, 6.0f);

    // Rejilla de frecuencia
    g.setFont (10.0f);
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
    {
        g.setColour (P.ink.withAlpha (0.07f));
        g.drawVerticalLine ((int) xForFreq (f), 0.0f, area.getHeight());
        g.setColour (P.inkMuted);
        g.drawText (f >= 1000.f ? juce::String (f / 1000.f) + "k" : juce::String (f),
                    (int) xForFreq (f) + 2, (int) area.getHeight() - 14, 36, 12, juce::Justification::left);
    }

    // Rejilla de dB (el paso depende del rango elegido)
    const float step = R <= 6.0f ? 3.0f : (R <= 12.0f ? 6.0f : 12.0f);
    for (int k = -1; k <= 1; ++k)
    {
        const float d = (float) k * step;
        g.setColour (P.ink.withAlpha (k == 0 ? 0.25f : 0.07f));
        g.drawHorizontalLine ((int) yForDb (d), 0.0f, area.getWidth());
        g.setColour (P.inkMuted);
        g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d), 4, (int) yForDb (d) - 12, 30, 12, juce::Justification::left);
    }

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;

    // Analizador (pre o post EQ), escala fija de -100 a 0 dBFS
    if ((int) proc.apvts.getRawParameterValue (EQ::analyzerId)->load() != 0)
    {
        auto buildPath = [&] (const std::array<float, numPoints>& data, bool fill)
        {
            juce::Path sp;
            for (int j = 0; j < numPoints; ++j)
            {
                const float x = xForFreq (shownFreq[(size_t) j]);
                const float y = area.getHeight() * (-juce::jlimit (-100.0f, 0.0f, data[(size_t) j]) / 100.0f);
                if (j == 0) { if (fill) { sp.startNewSubPath (x, area.getHeight()); sp.lineTo (x, y); } else sp.startNewSubPath (x, y); }
                else sp.lineTo (x, y);
            }
            if (fill) { sp.lineTo (sp.getCurrentPosition().x, area.getHeight()); sp.closeSubPath(); }
            return sp;
        };

        g.setColour (P.ink.withAlpha (0.10f));
        g.fillPath (buildPath (shown, true));
        if (holdWasOn)
        {
            g.setColour (P.ink.withAlpha (0.35f));
            g.strokePath (buildPath (held, false), juce::PathStrokeType (1.0f));
        }
    }

    // Bandas dinámicas: zona sombreada entre la ganancia máxima y la que se aplica ahora.
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        if (! EQ::hasDyn (b)
            || proc.apvts.getRawParameterValue (EQ::dynId (b))->load() < 0.5f
            || proc.apvts.getRawParameterValue (EQ::onId (b))->load() < 0.5f)
            continue;

        const auto s = EQ::readSettings (b, proc.apvts, sr);
        if (! EQ::dynEligible (s.shape)) continue;
        const auto maxD = EQ::makeDesign (s, s.gainDb, sr);
        const auto liveD = EQ::makeDesign (s, proc.getDynamicGainDb (b), sr);

        auto yAt = [&] (const EQ::Design& d, double f)
        {
            const float db = juce::Decibels::gainToDecibels ((float) d.magnitude (f, sr), -60.0f);
            return yForDb (juce::jlimit (-R, R, db));
        };

        const int w = juce::jmax (2, (int) area.getWidth());
        juce::Path shade;
        for (int i = 0; i < w; i += 3)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (w - 1));
            if (i == 0) shade.startNewSubPath ((float) i, yAt (maxD, f)); else shade.lineTo ((float) i, yAt (maxD, f));
        }
        for (int i = ((w - 1) / 3) * 3; i >= 0; i -= 3)
            shade.lineTo ((float) i, yAt (liveD, 20.0 * std::pow (1000.0, (double) i / (w - 1))));
        shade.closeSubPath();
        g.setColour (P.band[(size_t) b].withAlpha (0.28f));
        g.fillPath (shade);
    }

    // Respuesta: producto de las bandas estéreo, más las de Mid, Side, izquierdo o derecho según el canal.
    const int w = juce::jmax (2, (int) area.getWidth());
    std::vector<std::vector<float>> bandDb ((size_t) EQ::NumBands, std::vector<float> ((size_t) w, 0.0f));   // dB de cada banda en cada pixel
    int where[EQ::NumBands];
    bool anyMS = false, anyLR = false;
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const bool on = proc.apvts.getRawParameterValue (EQ::onId (b))->load() > 0.5f;
        where[b] = on ? EQ::placement (b, proc.apvts) : 0;
        if (where[b] == 1 || where[b] == 2) anyMS = true;
        if (where[b] >= 3) anyLR = true;
        if (! on) continue;
        const auto design = EQ::makeBand (b, proc.apvts, sr);
        for (int i = 0; i < w; ++i)
            bandDb[(size_t) b][(size_t) i] = juce::Decibels::gainToDecibels ((float) design.magnitude (20.0 * std::pow (1000.0, (double) i / (w - 1)), sr), -90.0f);
    }

    auto drawResponse = [&] (int channel, juce::Colour colour)
    {
        // channel: 0 = solo bandas estéreo, 1 = + Mid, 2 = + Side, 3 = + izquierdo, 4 = + derecho
        juce::Path path;
        for (int i = 0; i < w; ++i)
        {
            float db = 0.0f;
            for (int b = 0; b < EQ::NumBands; ++b)
                if (where[b] == 0 || where[b] == channel) db += bandDb[(size_t) b][(size_t) i];
            db = juce::jlimit (-R, R, db);
            if (i == 0) path.startNewSubPath ((float) i, yForDb (db)); else path.lineTo ((float) i, yForDb (db));
        }
        g.setColour (colour.withAlpha (0.14f));   // resplandor, como un trazo fosforescente
        g.strokePath (path, juce::PathStrokeType (7.0f));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    };

    const auto midCol = P.trace, sideCol = juce::Colour (0xff4fa6a8), leftCol = juce::Colour (0xffe8694a), rightCol = juce::Colour (0xff6f9bdb);
    if (anyMS || anyLR)
    {
        float labelX = 40.0f;
        auto legend = [&] (const juce::String& text, juce::Colour c)
        {
            g.setFont (11.0f);
            g.setColour (c);
            g.drawText (text, (int) labelX, 6, 44, 14, juce::Justification::left);
            labelX += 40.0f;
        };
        if (anyMS) { drawResponse (1, midCol); drawResponse (2, sideCol); legend ("Mid", midCol); legend ("Side", sideCol); }
        if (anyLR) { drawResponse (3, leftCol); drawResponse (4, rightCol); legend ("L", leftCol); legend ("R", rightCol); }
    }
    else
        drawResponse (0, P.trace);

    // Modo de fase y latencia
    const int phase = (int) proc.apvts.getRawParameterValue (EQ::phaseId)->load();
    if (phase > 0)
    {
        const int lat = proc.getLatencySamples();
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.setColour (P.accent.withAlpha (0.9f));
        g.drawText (juce::String (phase == 1 ? "NATURAL PHASE" : "LINEAR PHASE") + "  " + juce::String (lat) + " samples ("
                        + juce::String (1000.0 * lat / sr, 0) + " ms)",
                    area.toNearestInt().withTrimmedRight (10).removeFromTop (22), juce::Justification::centredRight);
    }

    // Cristal del visor: viñeteado en los bordes, reflejo suave arriba y líneas de barrido muy tenues
    {
        juce::ColourGradient vignette (juce::Colours::transparentBlack, area.getCentreX(), area.getCentreY(),
                                       juce::Colours::black.withAlpha (0.45f), area.getX(), area.getY(), true);
        vignette.addColour (0.55, juce::Colours::transparentBlack);
        g.setGradientFill (vignette);
        g.fillRoundedRectangle (area, 6.0f);

        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), area.getX(), area.getY(),
                                                 juce::Colours::transparentWhite, area.getX(), area.getY() + area.getHeight() * 0.4f, false));
        g.fillRoundedRectangle (area.withHeight (area.getHeight() * 0.4f), 6.0f);

        g.setColour (juce::Colours::black.withAlpha (0.05f));
        for (float y = 1.0f; y < area.getHeight(); y += 3.0f)
            g.drawHorizontalLine ((int) y, 0.0f, area.getWidth());
    }

    // Asas de Q de la banda enfocada (campanas y notch)
    if (focusBand >= 0 && isBell (focusBand) && proc.apvts.getRawParameterValue (EQ::onId (focusBand))->load() > 0.5f)
    {
        const auto c = P.band[(size_t) focusBand];
        const auto p = nodePos (focusBand), l = handlePos (focusBand, -1), r = handlePos (focusBand, +1);
        g.setColour (c.withAlpha (0.45f));
        g.drawLine (l.x, p.y, r.x, p.y, 1.0f);
        for (auto h : { l, r })
        {
            g.setColour (P.screen);
            g.fillEllipse (h.x - 5.0f, h.y - 5.0f, 10.0f, 10.0f);
            g.setColour (c);
            g.drawEllipse (h.x - 5.0f, h.y - 5.0f, 10.0f, 10.0f, 1.6f);
        }
    }

    // Puntos de banda
    const int solo = (int) proc.apvts.getRawParameterValue (EQ::soloId)->load();
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const bool on = proc.apvts.getRawParameterValue (EQ::onId (b))->load() > 0.5f;
        const auto p = nodePos (b);
        const float r = (b == hovered || b == dragged) ? 8.0f : 6.0f;
        g.setColour (P.band[(size_t) b].withAlpha (on ? 1.0f : 0.5f));
        if (on) g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
        else    g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 2.0f);

        if (solo == b + 1)   // banda en solo: aro doble
            g.drawEllipse (p.x - r - 4, p.y - r - 4, 2 * r + 8, 2 * r + 8, 2.2f);

        if (EQ::hasDyn (b) && on && proc.apvts.getRawParameterValue (EQ::dynId (b))->load() > 0.5f)   // anillo = banda dinámica
        {
            g.drawEllipse (p.x - r - 4, p.y - r - 4, 2 * r + 8, 2 * r + 8, 1.2f);

            // Punto blanco = ganancia que se está aplicando ahora mismo (entre 0 dB y el máximo que marca el punto de color).
            const float liveY = juce::jlimit (0.0f, (float) getHeight(), yForDb (proc.getDynamicGainDb (b)));
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawLine (p.x, p.y, p.x, liveY, 1.5f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (p.x - 3.5f, liveY - 3.5f, 7.0f, 7.0f);
        }
    }
}

//==============================================================================
void ResponseCurve::setParam (const juce::String& id, float v)
{
    if (auto* p = param (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void ResponseCurve::gesture (int b, bool begin)
{
    juce::StringArray ids { EQ::freqId (b) };
    if (! EQ::isCut (b)) ids.add (EQ::gainId (b));
    for (auto& id : ids)
        if (auto* p = param (id))
        {
            if (begin) p->beginChangeGesture(); else p->endChangeGesture();
        }
}

void ResponseCurve::qGesture (int b, bool begin)
{
    if (auto* p = param (EQ::qId (b)))
    {
        if (begin) p->beginChangeGesture(); else p->endChangeGesture();
    }
}

void ResponseCurve::mouseMove (const juce::MouseEvent& e)
{
    if (handleAt (e.position) != 0)
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    const int h = nodeAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        if (h >= 0) focusBand = h;
        repaint();
    }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void ResponseCurve::mouseExit (const juce::MouseEvent&) { hovered = -1; repaint(); }

void ResponseCurve::rebaseDrag (juce::Point<float> p)
{
    dragOrigin = p;
    dragStartFreq = proc.apvts.getRawParameterValue (EQ::freqId (dragged))->load();
    dragStartGain = EQ::isCut (dragged) ? 0.0f : proc.apvts.getRawParameterValue (EQ::gainId (dragged))->load();
}

void ResponseCurve::mouseDown (const juce::MouseEvent& e)
{
    proc.undoManager.beginNewTransaction();
    dragHandle = handleAt (e.position);
    if (dragHandle != 0)
    {
        qGesture (focusBand, true);
        return;
    }

    dragged = nodeAt (e.position);
    if (dragged >= 0)
    {
        focusBand = dragged;
        gesture (dragged, true);
        dragFine = e.mods.isShiftDown();
        rebaseDrag (e.position);
    }
}

void ResponseCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragHandle != 0)
    {
        // El ancho que marca el ratón fija la Q efectiva; se descuenta el factor del estilo para obtener la Q del parámetro.
        const auto s = EQ::readSettings (focusBand, proc.apvts, 48000.0);
        const float oct = juce::jlimit (0.05f, 6.0f, 2.0f * std::abs (std::log2 (freqForX (e.position.x) / s.freq)));
        const float k = s.shape == EQ::NotchShape ? 1.0f : EQ::styleQ (s.style, EQ::Peak, 1.0f, s.gainDb);
        setParam (EQ::qId (focusBand), juce::jlimit (0.1f, 10.0f, qForOctaves (oct) / k));
        return;
    }

    if (dragged < 0) return;

    // Con Mayús el punto se mueve diez veces más despacio (ajuste fino).
    if (e.mods.isShiftDown() != dragFine) { dragFine = e.mods.isShiftDown(); rebaseDrag (e.position); }
    const float scale = dragFine ? 0.1f : 1.0f;
    const auto delta = (e.position - dragOrigin) * scale;

    const float startX = xForFreq (dragStartFreq);
    setParam (EQ::freqId (dragged), juce::jlimit (20.0f, 20000.0f, freqForX (startX + delta.x)));
    if (! EQ::isCut (dragged) && ! isNotch (dragged))
        setParam (EQ::gainId (dragged), juce::jlimit (-18.0f, 18.0f, dragStartGain - delta.y * 2.0f * rangeDb() / (float) getHeight()));
}

void ResponseCurve::mouseUp (const juce::MouseEvent&)
{
    if (dragHandle != 0 && focusBand >= 0) qGesture (focusBand, false);
    if (dragged >= 0) gesture (dragged, false);
    dragged = -1;
    dragHandle = 0;
}

void ResponseCurve::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (handleAt (e.position) != 0) return;
    const int b = nodeAt (e.position);
    if (b < 0) return;
    if (auto* p = param (EQ::onId (b)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void ResponseCurve::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int b = nodeAt (e.position);
    if (b < 0 || EQ::isCut (b)) return;   // los pasos alto/bajo no tienen Q: su pendiente se elige abajo
    if (auto* p = param (EQ::qId (b)))
    {
        const float q = proc.apvts.getRawParameterValue (EQ::qId (b))->load() * std::exp (wheel.deltaY);
        proc.undoManager.beginNewTransaction();
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.1f, 10.0f, q)));
        p->endChangeGesture();
    }
}

//==============================================================================
void DynMeter::paint (juce::Graphics& g)
{
    const auto& P = paletteOf (*this);
    auto r = getLocalBounds().toFloat();
    g.setColour (P.screen);
    g.fillRoundedRectangle (r, 3.0f);

    const bool on = proc.apvts.getRawParameterValue (EQ::dynId (band))->load() > 0.5f
                    && proc.apvts.getRawParameterValue (EQ::onId (band))->load() > 0.5f;
    const float maxDb = proc.apvts.getRawParameterValue (EQ::gainId (band))->load();
    const float liveDb = proc.getDynamicGainDb (band);

    if (on && std::abs (maxDb) > 0.05f)
    {
        const float frac = juce::jlimit (0.0f, 1.0f, std::abs (liveDb) / std::abs (maxDb));
        g.setColour (P.band[(size_t) band].withAlpha (0.75f));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * frac), 3.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.4f));
    g.setFont (11.0f);
    g.drawText (on ? juce::String (liveDb, 1) + " / " + juce::String (maxDb, 1) + " dB" : juce::String ("off"),
                getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
//==============================================================================
MedidoresEQAudioProcessorEditor::MedidoresEQAudioProcessorEditor (MedidoresEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), presets (p.apvts), curve (p), inMeter (p, true), outMeter (p, false), meterPanel (p)
{
    laf.setPalette (Themes::get());
    setLookAndFeel (&laf);
    const auto& P = laf.getPalette();

    addAndMakeVisible (presetBox);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (deleteButton);
    presetBox.setTooltip ("Factory and user presets.");
    saveButton.setTooltip ("Save the current settings as a user preset.");
    presetBox.onChange = [this] { presetChosen(); };
    saveButton.onClick = [this] { askPresetName(); };
    deleteButton.onClick = [this] { askDeletePreset(); };
    menuButton.setTooltip ("More preset actions: import a preset file, export the current settings to a file, open the presets folder. "
                           "To organize presets in folders, save them as \"Folder/Name\".");
    menuButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (menuButton);
    helpButton.setTooltip ("Help: while it is on, the bar at the bottom describes whatever control is under the mouse.");
    helpButton.onClick = [this] { helpOn = ! helpOn; helpButton.setToggleState (helpOn, juce::dontSendNotification); helpText = {}; repaint(); };
    addAndMakeVisible (helpButton);
    refreshPresets();

    // Ajustes A/B/C/D y deshacer
    for (int i = 0; i < 4; ++i)
    {
        slotButton[i].setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
        slotButton[i].setTooltip ("Settings slot A/B/C/D: stores the current state and loads the slot's. "
                                  "An empty slot starts from the current settings, so you can compare a tweak with the original.");
        slotButton[i].onClick = [this, i] { proc.switchSlot (i); };
        addAndMakeVisible (slotButton[i]);
    }
    undoButton.setTooltip ("Undo the last settings change.");
    redoButton.setTooltip ("Redo the undone change.");
    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    // Ajustes de la vista
    addCombo (analyzerBox, analyzerAttachment, EQ::analyzerId, EQ::analyzerNames());
    addCombo (speedBox, speedAttachment, EQ::analyzerSpeedId, EQ::speedNames());
    addCombo (resBox, resAttachment, EQ::analyzerResId, EQ::analyzerResNames());
    addCombo (smoothBox, smoothAttachment, EQ::analyzerSmoothId, EQ::analyzerSmoothNames());
    addCombo (holdBox, holdAttachment, EQ::analyzerHoldId, EQ::holdNames());
    addCombo (rangeBox, rangeAttachment, EQ::rangeId, EQ::rangeNames());
    analyzerBox.setTooltip ("Spectrum analyzer: off, after the EQ (post) or before it (pre).");
    speedBox.setTooltip ("How fast the spectrum updates.");
    resBox.setTooltip ("Analyzer resolution (FFT size): Fine and Max separate the low end better.");
    smoothBox.setTooltip ("Spectrum smoothing in fractions of an octave.");
    holdBox.setTooltip ("Peak hold: a thin line keeps the maximum reached (cleared when you change the option).");
    rangeBox.setTooltip ("Vertical range of the curve (only changes what you see).");

    addAndMakeVisible (curve);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (meterPanel);

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        toggles[b].setButtonText (EQ::bands[b].name);
        toggles[b].setTooltip ("Turns the band on or off.");
        toggleAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::onId (b), toggles[b]);
        addAndMakeVisible (toggles[b]);

        soloButton[b].setButtonText ("S");
        soloButton[b].setComponentID ("solo" + juce::String (b));
        soloButton[b].setTooltip ("Solo: you only hear what this band touches (for a high/low-pass, the part it cuts).");
        soloButton[b].onClick = [this, b]
        {
            if (soloAttachment != nullptr) soloAttachment->setValueAsCompleteGesture (soloIndex == b + 1 ? 0.0f : (float) (b + 1));
        };
        addAndMakeVisible (soloButton[b]);

        addKnob (knobs[b][0], EQ::freqId (b), "Freq", 70, b, "Band frequency.");
        if (EQ::isCut (b))
        {
            slopeButtons[b] = std::make_unique<SegmentedButtons> (*proc.apvts.getParameter (EQ::slopeId (b)),
                                                                  juce::StringArray { "6", "12", "24", "48" }, P.band[(size_t) b]);
            slopeButtons[b]->setTooltip ("Filter slope in dB per octave: more dB, steeper cut.");
            addAndMakeVisible (*slopeButtons[b]);
        }
        else
        {
            addGainKnob (knobs[b][1], b, "Band gain. With dynamics on, this is the maximum.");
            addKnob (knobs[b][2], EQ::qId (b), "Q", 70, b, "Band width: higher Q, narrower.");

            placementButtons[b] = std::make_unique<SegmentedButtons> (*proc.apvts.getParameter (EQ::chId (b)),
                                                                      juce::StringArray { "ST", "M", "S", "L", "R" }, P.band[(size_t) b]);
            placementButtons[b]->setTooltip ("Where the band acts: ST = full stereo, M = Mid only, S = Side only, "
                                             "L = left channel only, R = right channel only.");
            addAndMakeVisible (*placementButtons[b]);

            typeButton[b] = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::typeId (b)), EQ::typeNames (b));
            typeButton[b]->setTooltip (EQ::isShelf (b)
                ? juce::String ("Type: Shelf, Bell, Pultec (boost and cut at once; the Q knob becomes the cut), "
                                "Tilt (tilts the whole spectrum around the frequency) or Baxandall (gentle 6 dB/oct shelf).")
                : juce::String ("Type: Bell or Notch (narrow notch, no gain)."));
            typeButton[b]->onChanged = [this, b] (int type) { refreshTypeUi (b, type); };
            addAndMakeVisible (*typeButton[b]);
        }

        if (EQ::hasDyn (b))
        {
            dynToggle[b].setButtonText ("Dynamic");
            dynToggle[b].setTooltip ("The gain only applies when the level in this band goes above the threshold (or below it, in expand mode).");
            dynAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::dynId (b), dynToggle[b]);
            addAndMakeVisible (dynToggle[b]);
            dynMeter[b] = std::make_unique<DynMeter> (proc, b);
            addAndMakeVisible (*dynMeter[b]);
            dmodeButton[b] = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::dmodeId (b)), EQ::dmodeNames());
            dmodeButton[b]->setTooltip ("Compress: acts when the level goes above the threshold. Expand: acts when the level falls below it.");
            addAndMakeVisible (*dmodeButton[b]);
            addKnob (thrKnob[b], EQ::thrId (b), "Thresh", 52, b, "Level in the band at which the dynamics start to act.");
            addKnob (ratioKnob[b], EQ::ratioId (b), "Ratio", 52, b, "How strongly the dynamics respond past the threshold.");
            addKnob (attackKnob[b], EQ::attackId (b), "Attack", 52, b, "How fast the dynamics start to act.");
            addKnob (releaseKnob[b], EQ::releaseId (b), "Release", 52, b, "How fast the band returns to its normal gain.");
        }
    }

    // Rango de los knobs de ganancia (±18/±12/±6/±3) y estado del solo
    gainRangeAttachment = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (EQ::gainRangeId), [this] (float v)
    {
        const double limit = (double) EQ::gainRangeFor (juce::roundToInt (v));
        for (auto& a : gainAttachments) if (a != nullptr) a->setLimit (limit);
    });
    gainRangeAttachment->sendInitialUpdate();
    soloAttachment = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (EQ::soloId), [this] (float v)
    {
        soloIndex = juce::roundToInt (v);
        for (int b = 0; b < EQ::NumBands; ++b) soloButton[b].setToggleState (soloIndex == b + 1, juce::dontSendNotification);
    });
    soloAttachment->sendInitialUpdate();

    for (int b = 0; b < EQ::NumBands; ++b)
        if (typeButton[b] != nullptr) typeButton[b]->refresh();   // coloca el knob Q/atenuación y los activos según el tipo

    addKnob (inKnob, EQ::inId, "Gain", 70, kAccent, "Input gain, before the EQ.");
    addKnob (outKnob, EQ::outId, "Gain", 70, kAccent, "Output gain, after the saturation.");
    addKnob (driveKnob, EQ::driveId, "Drive", 70, kWarm, "Amount of saturation (0 % = clean).");
    addKnob (mixKnob, EQ::mixId, "Mix", 70, kWarm,
             "Blend between the unsaturated and the saturated signal: below 100 % it is parallel saturation.");
    addKnob (monoKnob, EQ::monoFreqId, "Bass mono", 70, kAccent,
             "Below this frequency the Side is removed (mono bass). Off at the minimum.");
    addKnob (widthKnob, EQ::widthId, "Width", 70, kAccent, "Stereo width: 0 % mono, 100 % unchanged, 200 % double Side.");

    characterSwitch = std::make_unique<RotarySwitch> (*proc.apvts.getParameter (EQ::characterId), "SATURATION",
                                                      juce::StringArray { "CLEAN", "TAPE", "TUBE" }, P.accent);
    characterSwitch->setTooltip ("Saturation type: clean, tape or tube.");
    addAndMakeVisible (*characterSwitch);
    styleButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::styleId), juce::StringArray { "Modern", "Classic", "Amer.", "Vintage" }, "Style: ");
    styleButton->setTooltip ("Curve style: Modern (constant Q), Classic (wider as gain goes up), American (narrower as gain goes up) or Vintage.");
    addAndMakeVisible (*styleButton);
    phaseSwitch = std::make_unique<RotarySwitch> (*proc.apvts.getParameter (EQ::phaseId), "PHASE",
                                                  juce::StringArray { "MIN", "NAT", "LIN" }, P.accent);
    phaseSwitch->setTooltip ("EQ phase: Minimum (no latency, like an analog EQ), Natural (partial phase: less pre-ringing than linear) "
                             "or Linear (no phase shift between frequencies, with latency). Dynamic bands are always minimum phase.");
    addAndMakeVisible (*phaseSwitch);
    osButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::osId), EQ::osNames(), "Oversampling ");
    osButton->setTooltip ("Saturation oversampling: 2x or 4x (cleaner, a little more latency and CPU).");
    addAndMakeVisible (*osButton);
    gainRangeButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::gainRangeId), EQ::gainRangeNames(), "Range ");
    gainRangeButton->setTooltip ("Range of the gain knobs: with +/-6 or +/-3 dB the same travel gives more precision.");
    addAndMakeVisible (*gainRangeButton);
    ditherButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::ditherId), EQ::ditherNames(), "Dither: ");
    ditherButton->setTooltip ("TPDF dither at the output, for when the result will be saved at 16 or 24 bits.");
    addAndMakeVisible (*ditherButton);
    qualityButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::qualityId), EQ::qualityNames(), "Quality: ");
    qualityButton->setTooltip ("Length of the filter used by Natural and Linear phase. Low: about half the latency, less resolution in the lows. "
                               "High: double the latency, more precise lows. Changing it fades the sound out and in.");
    addAndMakeVisible (*qualityButton);
    scButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::scId), juce::StringArray { "Int", "Ext" }, "Sidechain: ");
    scButton->setTooltip ("Signal the dynamics listen to: the plugin's own (internal) or the host sidechain input (external).");
    addAndMakeVisible (*scButton);
    detButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::detId), EQ::detNames(), "Detector: ");
    detButton->setTooltip ("Level detection in the dynamics: peak (fast) or RMS (smoother, closer to how we hear).");
    addAndMakeVisible (*detButton);

    bypassToggle.setButtonText ("Bypass");
    bypassToggle.setTooltip ("Compare with the original: the original is delayed by the same latency as the processed signal, so there is no time offset.");
    bypassAttachment = std::make_unique<ButtonAttachment> (proc.apvts, EQ::bypassId, bypassToggle);
    addAndMakeVisible (bypassToggle);
    dcToggle.setButtonText ("DC filter");
    dcToggle.setTooltip ("Removes DC offset at the input (5 Hz high-pass, before the EQ). Bypass is not affected.");
    dcAttachment = std::make_unique<ButtonAttachment> (proc.apvts, EQ::dcId, dcToggle);
    addAndMakeVisible (dcToggle);
    deltaToggle.setButtonText ("Delta");
    deltaToggle.setTooltip ("Delta: you only hear the difference between the processed and the original signal (level matched, time aligned). "
                            "Useful to hear exactly what the EQ is changing.");
    deltaAttachment = std::make_unique<ButtonAttachment> (proc.apvts, EQ::deltaId, deltaToggle);
    addAndMakeVisible (deltaToggle);
    monoButton = std::make_unique<ParamToggleButton> (*proc.apvts.getParameter (EQ::monMonoId), "MONO");
    monoButton->setTooltip ("Monitor: sums the output to mono to check compatibility. The meters keep measuring the program signal.");
    swapButton = std::make_unique<ParamToggleButton> (*proc.apvts.getParameter (EQ::monSwapId), "SWAP");
    swapButton->setTooltip ("Monitor: swaps the left and right channels.");
    polLButton = std::make_unique<ParamToggleButton> (*proc.apvts.getParameter (EQ::monPolLId), "POL L");
    polLButton->setTooltip ("Monitor: inverts the polarity of the left channel.");
    polRButton = std::make_unique<ParamToggleButton> (*proc.apvts.getParameter (EQ::monPolRId), "POL R");
    polRButton->setTooltip ("Monitor: inverts the polarity of the right channel.");
    for (auto* b : { monoButton.get(), swapButton.get(), polLButton.get(), polRButton.get() }) addAndMakeVisible (*b);
    autoGainToggle.setButtonText ("Match");
    autoGainToggle.setTooltip ("Compensates the loudness (over 3 s) of the processed signal so it sounds as loud as the original when comparing.");
    autoGainAttachment = std::make_unique<ButtonAttachment> (proc.apvts, EQ::autoGainId, autoGainToggle);
    addAndMakeVisible (autoGainToggle);

    // Los ajustes de la dinámica (umbral, ratio, ataque, release) se despliegan: la ventana crece al abrirlos.
    dynOpen = (bool) proc.apvts.state.getProperty ("dynOpen", false);
    dynExpandButton.setTooltip ("Show or hide the dynamics settings of each band. The window grows when they are shown.");
    dynExpandButton.onClick = [this] { setDynamicsOpen (! dynOpen); };
    addAndMakeVisible (dynExpandButton);

    applyBandColours();
    setDynamicsOpen (dynOpen);
    startTimerHz (20);
}

MedidoresEQAudioProcessorEditor::~MedidoresEQAudioProcessorEditor()
{
    stopTimer();
    for (auto& a : gainAttachments) a.reset();
    setLookAndFeel (nullptr);
}

void MedidoresEQAudioProcessorEditor::timerCallback()
{
    if (helpOn)
    {
        juce::String tip = "Hover over a control to see what it does.";
        if (auto* c = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse())
            if (isParentOf (c) || c == this)
                for (auto* comp = c; comp != nullptr && comp != this; comp = comp->getParentComponent())
                    if (auto* tc = dynamic_cast<juce::TooltipClient*> (comp))
                        if (tc->getTooltip().isNotEmpty()) { tip = tc->getTooltip(); break; }
        if (tip != helpText) { helpText = tip; repaint (helpRect); }
    }

    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
    for (int i = 0; i < 4; ++i)
        slotButton[i].setToggleState (i == proc.getActiveSlot(), juce::dontSendNotification);
}

int MedidoresEQAudioProcessorEditor::windowHeight (bool dynamicsOpen)
{
    return dynamicsOpen ? 828 : 680;   // la ventana desplegada añade los ajustes de dinámica (148 px más que la fila compacta)
}

void MedidoresEQAudioProcessorEditor::setDynamicsOpen (bool open)
{
    dynOpen = open;
    proc.apvts.state.setProperty ("dynOpen", open, nullptr);
    dynExpandButton.setButtonText (open ? EQ::utf8 ("▾  Hide dynamics settings") : EQ::utf8 ("▸  Dynamics settings"));

    for (int b = 0; b < EQ::NumBands; ++b)
        if (EQ::hasDyn (b))
        {
            for (auto* k : { &thrKnob[b], &ratioKnob[b], &attackKnob[b], &releaseKnob[b] })
            {
                k->slider.setVisible (open);
                k->label.setVisible (open);
            }
            dmodeButton[b]->setVisible (open);
        }

    setSize (windowWidth, windowHeight (open));
}

// Muestra el knob Q o el de atenuación (Pultec) y desactiva los que no cuentan en el tipo elegido.
void MedidoresEQAudioProcessorEditor::refreshTypeUi (int b, int type)
{
    auto& qk = knobs[b][2];
    if (EQ::isShelf (b))
    {
        const bool pultec = type == 2;
        qk.attachment.reset();
        qk.attachment = std::make_unique<SliderAttachment> (proc.apvts, pultec ? EQ::cutId (b) : EQ::qId (b), qk.slider);
        if (auto* prm = proc.apvts.getParameter (pultec ? EQ::cutId (b) : EQ::qId (b)))
            qk.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        qk.label.setText (pultec ? "CUT" : "Q", juce::dontSendNotification);
        qk.slider.setTooltip (pultec ? "Pultec cut, at the same frequency as the boost. Double-click: default value."
                                     : "Band width: higher Q, narrower. Double-click: default value.");
        const bool enabled = type != 4;   // el Baxandall tiene la pendiente fija
        qk.slider.setEnabled (enabled);
        qk.label.setEnabled (enabled);
        styleKnob (qk);
    }
    else
    {
        const bool notch = type == 1;   // el notch no lleva ganancia
        knobs[b][1].slider.setEnabled (! notch);
        knobs[b][1].label.setEnabled (! notch);
    }
}

// Colores que dependen del tema: capuchones de los knobs, puntas de las palancas y pilotos de los pulsadores.
void MedidoresEQAudioProcessorEditor::applyBandColours()
{
    const auto& P = laf.getPalette();
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const auto c = P.band[(size_t) b];
        toggles[b].setColour (juce::ToggleButton::tickColourId, c);
        dynToggle[b].setColour (juce::ToggleButton::tickColourId, c);
        soloButton[b].setColour (juce::TextButton::buttonOnColourId, c);
        if (slopeButtons[b] != nullptr)     slopeButtons[b]->setLampColour (c);
        if (placementButtons[b] != nullptr) placementButtons[b]->setLampColour (c);

        for (auto* k : { &knobs[b][0], &knobs[b][1], &knobs[b][2], &thrKnob[b], &ratioKnob[b], &attackKnob[b], &releaseKnob[b] })
            if (k->capIndex != kUnused) styleKnob (*k);
    }
    for (auto* k : { &inKnob, &outKnob, &driveKnob, &mixKnob, &monoKnob, &widthKnob }) styleKnob (*k);
    bypassToggle.setColour (juce::ToggleButton::tickColourId, P.vuRed.brighter (0.3f));
    autoGainToggle.setColour (juce::ToggleButton::tickColourId, P.accent);
    dcToggle.setColour (juce::ToggleButton::tickColourId, P.accent);
    deltaToggle.setColour (juce::ToggleButton::tickColourId, P.vuRed.brighter (0.3f));
    for (auto* b : { monoButton.get(), swapButton.get(), polLButton.get(), polRButton.get() })
        if (b != nullptr) b->setColour (juce::TextButton::buttonOnColourId, P.accent);
    helpButton.setColour (juce::TextButton::buttonOnColourId, P.accent);
    for (auto& sb : slotButton) sb.setColour (juce::TextButton::buttonOnColourId, P.accent);
    for (auto* s : { characterSwitch.get(), phaseSwitch.get() })
        if (s != nullptr) s->setCapColour (P.accent);
}

void MedidoresEQAudioProcessorEditor::styleKnob (Knob& k)
{
    const auto& P = laf.getPalette();
    const auto cap = k.capIndex >= 0 ? P.band[(size_t) k.capIndex]
                                     : (k.capIndex == kWarm ? P.band[1].interpolatedWith (P.accent, 0.5f) : P.accent);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, cap);
    // Ventana empotrada para el valor
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::black.withAlpha (0.7f));
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, P.inset);
    k.slider.setColour (juce::Slider::textBoxTextColourId, P.insetText);
}

void MedidoresEQAudioProcessorEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text, int textBoxWidth,
                                               int capIndex, const juce::String& tip)
{
    k.capIndex = capIndex;
    // El texto del valor (unidades y decimales) lo da el propio parámetro.
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, 18);
    k.slider.setTooltip (tip + " Double-click: default value. Shift + drag: fine adjustment. Click the value to type a number.");
    k.label.setText (text.toUpperCase(), juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setInterceptsMouseClicks (false, false);
    k.attachment = std::make_unique<SliderAttachment> (proc.apvts, id, k.slider);
    if (auto* p = proc.apvts.getParameter (id))
        k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    styleKnob (k);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

// Gain knob with its own range (±18/±12/±6/±3 depending on the "Range" setting).
void MedidoresEQAudioProcessorEditor::addGainKnob (Knob& k, int b, const juce::String& tip)
{
    k.capIndex = b;
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.slider.setTooltip (tip + " Double-click: 0 dB. Shift + drag: fine adjustment. Click the value to type a number.");
    k.label.setText ("GAIN", juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setInterceptsMouseClicks (false, false);
    gainAttachments[b] = std::make_unique<RangedSliderAttachment> (*proc.apvts.getParameter (EQ::gainId (b)), k.slider);
    styleKnob (k);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

void MedidoresEQAudioProcessorEditor::addCombo (juce::ComboBox& box, std::unique_ptr<ComboAttachment>& att,
                                                const juce::String& id, const juce::StringArray& items)
{
    box.addItemList (items, 1);
    att = std::make_unique<ComboAttachment> (proc.apvts, id, box);
    addAndMakeVisible (box);
}

//==============================================================================
// Desplegable de presets: los ids van de 1 en adelante, primero los de fábrica y luego los de usuario.
void MedidoresEQAudioProcessorEditor::refreshPresets (const juce::String& select)
{
    factoryNames = presets.factoryNames();
    userNames = presets.userNames();

    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < factoryNames.size(); ++i) presetBox.addItem (factoryNames[i], 1 + i);
    if (userNames.size() > 0)
    {
        // User presets: the ones in the root first, then one section per folder ("Folder/Name")
        struct Item { juce::String heading, label; int index; };
        std::vector<Item> items;
        for (int i = 0; i < userNames.size(); ++i)
        {
            const auto& n = userNames[i];
            const bool inFolder = n.contains ("/");
            items.push_back ({ inFolder ? n.upToFirstOccurrenceOf ("/", false, false) : juce::String ("User"),
                               inFolder ? n.fromLastOccurrenceOf ("/", false, false) : n, i });
        }
        std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b)
        {
            if ((a.heading == "User") != (b.heading == "User")) return a.heading == "User";
            return a.heading.compareIgnoreCase (b.heading) < 0;
        });

        presetBox.addSeparator();
        juce::String current;
        for (const auto& it : items)
        {
            if (it.heading != current) { presetBox.addSectionHeading (it.heading); current = it.heading; }
            presetBox.addItem (it.label, 1001 + it.index);
        }
    }
    presetBox.setTextWhenNothingSelected ("Presets...");

    const int idx = userNames.indexOf (select);
    if (idx >= 0) presetBox.setSelectedId (1001 + idx, juce::dontSendNotification);
    deleteButton.setEnabled (idx >= 0);
}

void MedidoresEQAudioProcessorEditor::presetChosen()
{
    proc.undoManager.beginNewTransaction();
    const int id = presetBox.getSelectedId();
    if (id >= 1001) presets.loadUser (userNames[id - 1001]);
    else if (id >= 1) presets.loadFactory (factoryNames[id - 1]);
    deleteButton.setEnabled (id >= 1001);
}

void MedidoresEQAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Import preset file...");
    menu.addItem (2, "Export current settings...");
    menu.addSeparator();
    menu.addItem (3, "Open presets folder");

    juce::Component::SafePointer<MedidoresEQAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [safe] (int result)
    {
        if (safe == nullptr || result == 0) return;

        if (result == 3)
        {
            PresetManager::folder().createDirectory();
            PresetManager::folder().revealToUser();
        }
        else if (result == 1)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("Import preset", juce::File(), "*.xml");
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto file = fc.getResult();
                if (! file.existsAsFile()) return;
                const auto name = safe->presets.importFrom (file);
                if (name.isNotEmpty()) safe->refreshPresets (name);
                else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import preset", "That file is not an eCU-10 MST preset.");
            });
        }
        else if (result == 2)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("Export settings",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("eCU-10 MST preset.xml"), "*.xml");
            safe->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                            | juce::FileBrowserComponent::warnAboutOverwriting,
                                        [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto file = fc.getResult();
                if (file != juce::File() && ! safe->presets.exportTo (file))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export settings", "The file could not be written.");
            });
        }
    });
}

void MedidoresEQAudioProcessorEditor::askPresetName()
{
    auto* w = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "", "");
    w->addTextBlock ("Tip: use \"Folder/Name\" to save the preset inside a folder.");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MedidoresEQAudioProcessorEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (safe->presets.saveUser (name))
            safe->refreshPresets (name);
    }), true);
}

void MedidoresEQAudioProcessorEditor::askDeletePreset()
{
    const int id = presetBox.getSelectedId();
    if (id < 1001) return;
    const auto name = userNames[id - 1001];

    juce::Component::SafePointer<MedidoresEQAudioProcessorEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                        "Delete the preset \"" + name + "\"?", "Delete", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safe, name] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        safe->presets.removeUser (name);
        safe->refreshPresets();
    }));
}

//==============================================================================
void MedidoresEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& P = laf.getPalette();
    if (plate.isNull() || plate.getWidth() != getWidth() || plate.getHeight() != getHeight())
        plate = makePlate (getWidth(), getHeight(), P);
    g.drawImageAt (plate, 0, 0);

    // Orejas de rack con sus tornillos
    constexpr int earW = 18;
    for (int side = 0; side < 2; ++side)
    {
        const float x = side == 0 ? 0.0f : (float) (getWidth() - earW);
        const juce::Rectangle<float> ear (x, 0.0f, (float) earW, (float) getHeight());
        g.setGradientFill (juce::ColourGradient (P.ear.brighter (0.2f), ear.getX(), 0.0f, P.ear.darker (0.3f), ear.getRight(), 0.0f, false));
        g.fillRect (ear);
        const float edge = side == 0 ? ear.getRight() - 0.75f : ear.getX() + 0.75f;
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.drawLine (edge, 0.0f, edge, (float) getHeight(), 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        const float hi = side == 0 ? ear.getX() + 1.0f : ear.getRight() - 1.0f;
        g.drawLine (hi, 0.0f, hi, (float) getHeight(), 1.0f);
        for (float y : { 20.0f, (float) getHeight() * 0.5f, (float) getHeight() - 20.0f })
            drawScrew (g, { ear.getCentreX(), y }, 5.0f, 0.5f + y * 0.013f);
    }

    // Marco del visor de la curva, con sus tornillos
    {
        const auto r = bezelRect.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 10.0f);
        g.setGradientFill (juce::ColourGradient (P.ear.brighter (0.45f), r.getX(), r.getY(), P.ear.darker (0.5f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 10.0f, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (r.reduced (1.2f), 9.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));   // hueco hundido del cristal
        g.drawRoundedRectangle (curve.getBounds().toFloat().expanded (1.5f), 7.0f, 3.0f);
        for (auto c : { juce::Point<float> (r.getX() + 6.0f, r.getY() + 6.0f), juce::Point<float> (r.getRight() - 6.0f, r.getY() + 6.0f),
                        juce::Point<float> (r.getX() + 6.0f, r.getBottom() - 6.0f), juce::Point<float> (r.getRight() - 6.0f, r.getBottom() - 6.0f) })
            drawScrew (g, c, 3.2f, c.x * 0.02f);
    }

    // Recuadros serigrafiados de cada sección
    for (const auto& s : sections)
    {
        const auto r = s.bounds.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.28f));   // el grabado: una sombra y la línea de tinta
        g.drawRoundedRectangle (r.translated (0.0f, 1.2f), 8.0f, 1.4f);
        g.setColour (P.line.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 8.0f, 1.4f);

        if (s.title.isNotEmpty())
        {
            g.setColour (P.ink);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
            g.drawText (s.title, (int) r.getX(), sectionTitleY + 2, (int) r.getWidth(), 22, juce::Justification::centred);
        }
    }

    if (sections.size() > 1)   // separador entre el paso alto y el paso bajo
    {
        const auto r = sections[1].bounds.toFloat();
        g.setColour (P.line.withAlpha (0.45f));
        g.drawLine (r.getX() + 10.0f, (float) filterSplitY, r.getRight() - 10.0f, (float) filterSplitY, 1.2f);
    }

    if (helpOn)
    {
        g.setColour (P.accent);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText (helpText.isEmpty() ? juce::String ("Hover over a control to see what it does.") : helpText, helpRect,
                          juce::Justification::centredLeft, 2, 0.95f);
    }

    // Placa con el nombre del equipo
    {
        const juce::Rectangle<float> r ((float) stripRect.getX() + 2.0f, (float) stripRect.getY() + 2.0f, 262.0f, 26.0f);
        g.setGradientFill (juce::ColourGradient (P.ear.brighter (0.35f), r.getX(), r.getY(), P.ear.darker (0.35f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.drawLine (r.getX() + 3.0f, r.getY() + 1.5f, r.getRight() - 3.0f, r.getY() + 1.5f, 1.0f);
        g.setColour (P.ink);
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawText ("eCU-10 MST", r.withTrimmedRight (140.0f).translated (12.0f, 0.0f), juce::Justification::centredLeft, false);
        g.setColour (P.inkMuted);
        g.setFont (juce::Font (juce::FontOptions (9.5f)).withExtraKerningFactor (0.12f));
        g.drawText ("MASTERING EQ", r.withTrimmedLeft (r.getWidth() - 148.0f), juce::Justification::centred, false);
    }
}

void MedidoresEQAudioProcessorEditor::resized()
{
    constexpr int earW = 18;
    auto area = getLocalBounds().withTrimmedLeft (earW).withTrimmedRight (earW).reduced (8, 8);

    auto bar = area.removeFromTop (30);
    presetBox.setBounds (bar.removeFromLeft (180));
    bar.removeFromLeft (6);
    saveButton.setBounds (bar.removeFromLeft (60));
    bar.removeFromLeft (4);
    deleteButton.setBounds (bar.removeFromLeft (60));
    bar.removeFromLeft (4);
    menuButton.setBounds (bar.removeFromLeft (32));
    bar.removeFromLeft (14);
    for (int i = 0; i < 4; ++i)
    {
        slotButton[i].setBounds (bar.removeFromLeft (30));
        bar.removeFromLeft (3);
    }
    bar.removeFromLeft (8);
    undoButton.setBounds (bar.removeFromLeft (58));
    bar.removeFromLeft (4);
    redoButton.setBounds (bar.removeFromLeft (58));
    bar.removeFromLeft (10);
    helpButton.setBounds (bar.removeFromLeft (54));

    rangeBox.setBounds (bar.removeFromRight (78));
    bar.removeFromRight (6);
    holdBox.setBounds (bar.removeFromRight (90));
    bar.removeFromRight (6);
    smoothBox.setBounds (bar.removeFromRight (106));
    bar.removeFromRight (6);
    resBox.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (6);
    speedBox.setBounds (bar.removeFromRight (76));
    bar.removeFromRight (6);
    analyzerBox.setBounds (bar.removeFromRight (88));
    area.removeFromTop (8);

    bezelRect = area.removeFromTop (186);
    curve.setBounds (bezelRect.reduced (10, 9));
    area.removeFromTop (8);

    stripRect = area.removeFromBottom (30);
    area.removeFromBottom (6);
    dynExpandButton.setBounds (stripRect.getCentreX() - 105, stripRect.getY() + 2, 210, 26);
    helpRect = juce::Rectangle<int> (stripRect.getCentreX() + 120, stripRect.getY() + 1, stripRect.getRight() - stripRect.getCentreX() - 124, stripRect.getHeight() - 2);

    // Columnas (de izquierda a derecha): ENTRADA | FILTROS (paso alto arriba, paso bajo abajo) | Graves | Medio 1-4 | Agudos |
    //                                    CARÁCTER | MASTER | MEDICIÓN | SALIDA
    constexpr int numCols = 12;
    const int colW = area.getWidth() / numCols;
    const int x0 = area.getX();
    auto colX = [&] (int col) { return x0 + col * colW; };
    const int bandCol[EQ::NumBands] = { 1, 2, 3, 4, 5, 6, 7, 1 };   // paso alto y paso bajo comparten columna

    auto toggleRow = area.removeFromTop (26);
    auto comboRow = area.removeFromBottom (76);
    auto dynRow = area.removeFromBottom (dynOpen ? 200 : 52);   // palanca de dinámica + medidor (+ modo y 4 knobs al desplegar)
    const int rowH = area.getHeight() / 3;
    auto place = [] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r);
    };

    const int colTop = toggleRow.getY(), colBottom = comboRow.getBottom();
    const int filterBlockH = juce::jmin ((colBottom - colTop) / 2, 215);   // en la ventana desplegada no se estiran: el paso bajo va pegado al alto

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const int x = colX (bandCol[b]);

        if (EQ::isCut (b))
        {
            // Paso alto (arriba) y paso bajo (abajo) en la misma columna: palanca, frecuencia y pendiente.
            const int blockH = filterBlockH;
            const int blockTop = colTop + (b == EQ::LowPass ? blockH + 6 : 0);
            const int avail = blockH - (b == EQ::LowPass ? 6 : 0);
            const int knobH = juce::jmin (rowH + 14, avail - 26 - 24 - 20);

            toggles[b].setBounds (x + 8, blockTop + 2, colW - 12, 26);
            place (knobs[b][0], { x, blockTop + 34, colW, knobH });
            soloButton[b].setBounds (x + colW - 26, blockTop + 34, 22, 15);
            soloButton[b].toFront (false);
            slopeButtons[b]->setBounds (x + 8, blockTop + 34 + knobH + 8, colW - 16, 24);
            continue;
        }

        toggles[b].setBounds (x + 8, toggleRow.getY() + 2, colW - 12, toggleRow.getHeight());
        place (knobs[b][0], { x, area.getY(), colW, rowH });
        place (knobs[b][1], { x, area.getY() + rowH, colW, rowH });
        place (knobs[b][2], { x, area.getY() + 2 * rowH, colW, rowH });
        soloButton[b].setBounds (x + colW - 26, area.getY(), 22, 15);
        soloButton[b].toFront (false);

        dynToggle[b].setBounds (x + 8, dynRow.getY() + 2, colW - 12, 26);
        dynMeter[b]->setBounds (x + 10, dynRow.getY() + 30, colW - 20, 16);
        const int knobRowH = 60;
        place (thrKnob[b],     { x,            dynRow.getY() + 50,  colW / 2, knobRowH });
        place (ratioKnob[b],   { x + colW / 2, dynRow.getY() + 50,  colW / 2, knobRowH });
        place (attackKnob[b],  { x,            dynRow.getY() + 50 + knobRowH, colW / 2, knobRowH });
        place (releaseKnob[b], { x + colW / 2, dynRow.getY() + 50 + knobRowH, colW / 2, knobRowH });
        dmodeButton[b]->setBounds (x + 8, dynRow.getY() + 50 + 2 * knobRowH + 4, colW - 16, 22);

        typeButton[b]->setBounds (x + 8, comboRow.getY() + 8, colW - 16, 24);
        placementButtons[b]->setBounds (x + 4, comboRow.getY() + 38, colW - 8, 24);
    }

    // Columnas de los extremos, de carácter y de master
    const int inCol = colX (0), characterCol = colX (8), masterCol = colX (9), meterCol = colX (10), outCol = colX (11);
    const int bottomY = colBottom - 4;
    const int y3 = area.getY() + 2 * rowH, rest = comboRow.getY() - y3;

    place (driveKnob, { characterCol, area.getY(), colW, rowH });
    place (mixKnob,   { characterCol, area.getY() + rowH, colW, rowH });
    characterSwitch->setBounds (characterCol + 2, y3, colW - 4, rest - 30);
    styleButton->setBounds (characterCol + 6, y3 + rest - 28, colW - 12, 24);
    auto threeButtons = [&] (int colXpos, CycleButton& a, CycleButton& b, CycleButton& c)
    {
        a.setBounds (colXpos + 6, comboRow.getY() + 3, colW - 12, 22);
        b.setBounds (colXpos + 6, comboRow.getY() + 27, colW - 12, 22);
        c.setBounds (colXpos + 6, comboRow.getY() + 51, colW - 12, 22);
    };
    osButton->setBounds (characterCol + 6, comboRow.getY() + 3, colW - 12, 22);
    threeButtons (characterCol, *osButton, *gainRangeButton, *ditherButton);

    place (monoKnob,  { masterCol, area.getY(), colW, rowH });
    place (widthKnob, { masterCol, area.getY() + rowH, colW, rowH });
    phaseSwitch->setBounds (masterCol + 2, y3, colW - 4, rest);
    threeButtons (masterCol, *qualityButton, *scButton, *detButton);

    // Measurement column: goniometer, correlation and loudness above; monitoring tools below
    const int monitorH = 56;
    meterPanel.setBounds (meterCol + 2, area.getY() + 2, colW - 4, bottomY - area.getY() - 2 - monitorH - 4);
    {
        const int bw = (colW - 14) / 2, bx = meterCol + 6, by = bottomY - monitorH;
        monoButton->setBounds (bx, by, bw, 24);
        swapButton->setBounds (bx + bw + 2, by, bw, 24);
        polLButton->setBounds (bx, by + 28, bw, 24);
        polRButton->setBounds (bx + bw + 2, by + 28, bw, 24);
    }

    place (inKnob,  { inCol,  area.getY(), colW, rowH });
    place (outKnob, { outCol, area.getY(), colW, rowH });
    const int vuY = area.getY() + rowH + 4;
    inMeter.setBounds  (inCol  + 4, vuY, colW - 8, comboRow.getY() - vuY - 4);
    outMeter.setBounds (outCol + 4, vuY, colW - 8, comboRow.getY() - vuY - 4);
    bypassToggle.setBounds (inCol + 8, comboRow.getY() + 6, colW - 12, 26);
    dcToggle.setBounds (inCol + 8, comboRow.getY() + 40, colW - 12, 26);
    autoGainToggle.setBounds (outCol + 8, comboRow.getY() + 6, colW - 12, 26);
    deltaToggle.setBounds (outCol + 8, comboRow.getY() + 40, colW - 12, 26);

    // Recuadros serigrafiados: de la fila de interruptores al último control
    sections.clear();
    const int panelTop = colTop - 3, panelBottom = colBottom + 3;
    auto addSection = [&] (int col, const juce::String& title)
    {
        sections.push_back ({ { colX (col) + 2, panelTop, colW - 4, panelBottom - panelTop }, title });
    };
    addSection (0, "INPUT");
    addSection (1, {});
    for (int col = 2; col <= 7; ++col) addSection (col, {});
    addSection (8, "CHARACTER");
    addSection (9, "MASTER");
    addSection (10, "METERING");
    addSection (11, "OUTPUT");
    filterSplitY = colTop + filterBlockH + 2;
    sectionTitleY = toggleRow.getY() + 2;
}
