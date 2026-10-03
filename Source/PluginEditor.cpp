#include "PluginEditor.h"

//==============================================================================
ResponseCurve::ResponseCurve (MedidoresEQAudioProcessor& p) : proc (p)
{
    shown.fill (-120.0f);
    held.fill (-120.0f);
    setTooltip (EQ::utf8 ("Arrastra un punto: frecuencia y ganancia (con Mayús, ajuste fino). Rueda sobre un punto: Q. "
                          "Doble clic en un punto: activar o desactivar la banda. "
                          "Las asas laterales de la banda enfocada cambian su ancho (Q)."));
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
        g.drawText (juce::String (phase == 1 ? "FASE NATURAL" : "FASE LINEAL") + "  " + juce::String (lat) + " muestras ("
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
    g.drawText (on ? juce::String (liveDb, 1) + " / " + juce::String (maxDb, 1) + " dB" : juce::String ("apagada"),
                getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
//==============================================================================
MedidoresEQAudioProcessorEditor::MedidoresEQAudioProcessorEditor (MedidoresEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), presets (p.apvts), curve (p), inVU (p, true), outVU (p, false), meterPanel (p)
{
    laf.setPalette (Themes::get());
    setLookAndFeel (&laf);
    const auto& P = laf.getPalette();

    addAndMakeVisible (presetBox);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (deleteButton);
    presetBox.setTooltip (EQ::utf8 ("Presets de fábrica y de usuario."));
    saveButton.setTooltip (EQ::utf8 ("Guarda los ajustes actuales como preset de usuario."));
    presetBox.onChange = [this] { presetChosen(); };
    saveButton.onClick = [this] { askPresetName(); };
    deleteButton.onClick = [this] { askDeletePreset(); };
    refreshPresets();

    // Ajustes A/B/C/D y deshacer
    for (int i = 0; i < 4; ++i)
    {
        slotButton[i].setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
        slotButton[i].setTooltip (EQ::utf8 ("Ranura de ajustes A/B/C/D: guarda el estado actual y carga el de la ranura. "
                                            "Una ranura vacía parte de los ajustes actuales, así se compara un retoque con el original."));
        slotButton[i].onClick = [this, i] { proc.switchSlot (i); };
        addAndMakeVisible (slotButton[i]);
    }
    undoButton.setTooltip (EQ::utf8 ("Deshace el último cambio de ajustes."));
    redoButton.setTooltip (EQ::utf8 ("Rehace el cambio deshecho."));
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
    analyzerBox.setTooltip (EQ::utf8 ("Analizador de espectro: apagado, después del EQ (post) o antes (pre)."));
    speedBox.setTooltip (EQ::utf8 ("Rapidez con la que se actualiza el espectro."));
    resBox.setTooltip (EQ::utf8 ("Resolución del analizador (tamaño de la FFT): la fina y la máxima separan mejor los graves."));
    smoothBox.setTooltip (EQ::utf8 ("Suavizado del espectro por fracciones de octava."));
    holdBox.setTooltip (EQ::utf8 ("Con pico: una línea fina mantiene el máximo alcanzado (se borra al cambiar de opción)."));
    rangeBox.setTooltip (EQ::utf8 ("Rango vertical de la curva (solo cambia lo que se ve)."));

    addAndMakeVisible (curve);
    addAndMakeVisible (inVU);
    addAndMakeVisible (outVU);
    addAndMakeVisible (meterPanel);

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        toggles[b].setButtonText (EQ::bands[b].name);
        toggles[b].setTooltip (EQ::utf8 ("Activa o desactiva la banda."));
        toggleAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::onId (b), toggles[b]);
        addAndMakeVisible (toggles[b]);

        soloButton[b].setButtonText ("S");
        soloButton[b].setTooltip (EQ::utf8 ("Solo: escuchas únicamente lo que toca esta banda (en un paso alto o bajo, lo que recorta)."));
        soloButton[b].onClick = [this, b]
        {
            if (soloAttachment != nullptr) soloAttachment->setValueAsCompleteGesture (soloIndex == b + 1 ? 0.0f : (float) (b + 1));
        };
        addAndMakeVisible (soloButton[b]);

        addKnob (knobs[b][0], EQ::freqId (b), "Frec", 70, b, EQ::utf8 ("Frecuencia de la banda."));
        if (EQ::isCut (b))
        {
            slopeButtons[b] = std::make_unique<SegmentedButtons> (*proc.apvts.getParameter (EQ::slopeId (b)),
                                                                  juce::StringArray { "6", "12", "24", "48" }, P.band[(size_t) b]);
            slopeButtons[b]->setTooltip (EQ::utf8 ("Pendiente del filtro en dB por octava: más dB, corte más brusco."));
            addAndMakeVisible (*slopeButtons[b]);
        }
        else
        {
            addGainKnob (knobs[b][1], b, EQ::utf8 ("Ganancia de la banda. Con la dinámica activada es el máximo."));
            addKnob (knobs[b][2], EQ::qId (b), "Q", 70, b, EQ::utf8 ("Ancho de la banda: más Q, más estrecha."));

            placementButtons[b] = std::make_unique<SegmentedButtons> (*proc.apvts.getParameter (EQ::chId (b)),
                                                                      juce::StringArray { "ST", "M", "S", "L", "R" }, P.band[(size_t) b]);
            placementButtons[b]->setTooltip (EQ::utf8 ("Dónde actúa la banda: ST = estéreo completo, M = solo el Mid, S = solo el Side, "
                                                       "L = solo el canal izquierdo, R = solo el derecho."));
            addAndMakeVisible (*placementButtons[b]);

            typeButton[b] = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::typeId (b)), EQ::typeNames (b));
            typeButton[b]->setTooltip (EQ::isShelf (b)
                ? EQ::utf8 ("Tipo: Shelf, Campana, Pultec (realce y atenuación a la vez; el knob Q pasa a ser la atenuación), "
                            "Tilt (inclina todo el espectro alrededor de la frecuencia) o Baxandall (shelf suave de 6 dB/oct).")
                : EQ::utf8 ("Tipo: Campana o Notch (muesca estrecha, sin ganancia)."));
            typeButton[b]->onChanged = [this, b] (int type) { refreshTypeUi (b, type); };
            addAndMakeVisible (*typeButton[b]);
        }

        if (EQ::hasDyn (b))
        {
            dynToggle[b].setButtonText (EQ::utf8 ("Dinámica"));
            dynToggle[b].setTooltip (EQ::utf8 ("La ganancia solo se aplica cuando el nivel en esta banda supera el umbral (o queda por debajo, en expansión)."));
            dynAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::dynId (b), dynToggle[b]);
            addAndMakeVisible (dynToggle[b]);
            dynMeter[b] = std::make_unique<DynMeter> (proc, b);
            addAndMakeVisible (*dynMeter[b]);
            dmodeButton[b] = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::dmodeId (b)), EQ::dmodeNames());
            dmodeButton[b]->setTooltip (EQ::utf8 ("Compresión: actúa al superar el umbral. Expansión: actúa cuando el nivel cae por debajo."));
            addAndMakeVisible (*dmodeButton[b]);
            addKnob (thrKnob[b], EQ::thrId (b), "Umbral", 52, b, EQ::utf8 ("Nivel en la banda a partir del cual actúa la dinámica."));
            addKnob (ratioKnob[b], EQ::ratioId (b), "Ratio", 52, b, EQ::utf8 ("Cuánto responde la dinámica al superar el umbral."));
            addKnob (attackKnob[b], EQ::attackId (b), "Ataque", 52, b, EQ::utf8 ("Rapidez con la que la dinámica empieza a actuar."));
            addKnob (releaseKnob[b], EQ::releaseId (b), "Release", 52, b, EQ::utf8 ("Rapidez con la que la banda vuelve a su ganancia normal."));
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

    addKnob (inKnob, EQ::inId, "Gan", 70, kAccent, EQ::utf8 ("Ganancia de entrada, antes del EQ."));
    addKnob (outKnob, EQ::outId, "Gan", 70, kAccent, EQ::utf8 ("Ganancia de salida, después de la saturación."));
    addKnob (driveKnob, EQ::driveId, "Drive", 70, kWarm, EQ::utf8 ("Cantidad de saturación (0 % = limpio)."));
    addKnob (mixKnob, EQ::mixId, "Mezcla", 70, kWarm,
             EQ::utf8 ("Mezcla entre la señal sin saturar y la saturada: por debajo de 100 % es saturación en paralelo."));
    addKnob (monoKnob, EQ::monoFreqId, "Graves mono", 70, kAccent,
             EQ::utf8 ("Por debajo de esta frecuencia el Side se elimina (graves en mono). Apagado al mínimo."));
    addKnob (widthKnob, EQ::widthId, "Anchura", 70, kAccent, EQ::utf8 ("Anchura estéreo: 0 % mono, 100 % sin cambios, 200 % el doble de Side."));

    characterSwitch = std::make_unique<RotarySwitch> (*proc.apvts.getParameter (EQ::characterId), EQ::utf8 ("SATURACIÓN"),
                                                      juce::StringArray { "LIMPIO", "CINTA", EQ::utf8 ("VÁLV.") }, P.accent);
    characterSwitch->setTooltip (EQ::utf8 ("Tipo de saturación: limpio, cinta o válvula."));
    addAndMakeVisible (*characterSwitch);
    styleSwitch = std::make_unique<RotarySwitch> (*proc.apvts.getParameter (EQ::styleId), "CURVAS",
                                                  juce::StringArray { "MOD", EQ::utf8 ("CLÁS"), "AMER", "VINT" }, P.accent);
    styleSwitch->setTooltip (EQ::utf8 ("Estilo de curva: Moderna (Q constante), Clásica (ancha al subir ganancia), Americana (estrecha al subir) o Vintage."));
    addAndMakeVisible (*styleSwitch);
    phaseSwitch = std::make_unique<RotarySwitch> (*proc.apvts.getParameter (EQ::phaseId), "FASE",
                                                  juce::StringArray { EQ::utf8 ("MÍN"), "NAT", "LIN" }, P.accent);
    phaseSwitch->setTooltip (EQ::utf8 ("Fase del EQ: Mínima (sin latencia, como un EQ analógico), Natural (fase parcial: menos pre-eco que la lineal) "
                                       "o Lineal (sin desfase entre frecuencias, con latencia). Las bandas dinámicas siempre son de fase mínima."));
    addAndMakeVisible (*phaseSwitch);
    ditherSwitch = std::make_unique<RotarySwitch> (*proc.apvts.getParameter (EQ::ditherId), "DITHER",
                                                   juce::StringArray { "OFF", "16", "24" }, P.accent);
    ditherSwitch->setTooltip (EQ::utf8 ("Dither TPDF a la salida, por si el resultado se va a guardar a 16 o 24 bits."));
    addAndMakeVisible (*ditherSwitch);

    osButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::osId), EQ::osNames(), "Oversampling ");
    osButton->setTooltip (EQ::utf8 ("Sobremuestreo de la saturación: 2x o 4x (más limpio, algo más de latencia y CPU)."));
    addAndMakeVisible (*osButton);
    gainRangeButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::gainRangeId), EQ::gainRangeNames(), "Rango ");
    gainRangeButton->setTooltip (EQ::utf8 ("Rango de los knobs de ganancia: con ±6 o ±3 dB el mismo recorrido da más precisión."));
    addAndMakeVisible (*gainRangeButton);
    scButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::scId), EQ::scNames(), "Detector ");
    scButton->setTooltip (EQ::utf8 ("Señal que mide la dinámica: la propia (interno) o la entrada de sidechain del host (externo)."));
    addAndMakeVisible (*scButton);
    detButton = std::make_unique<CycleButton> (*proc.apvts.getParameter (EQ::detId), EQ::detNames(), "Medida ");
    detButton->setTooltip (EQ::utf8 ("Medida del nivel en la dinámica: pico (rápida) o RMS (más suave, parecida al oído)."));
    addAndMakeVisible (*detButton);

    bypassToggle.setButtonText ("Bypass");
    bypassToggle.setTooltip (EQ::utf8 ("Compara con el original: el original se retrasa lo mismo que el procesado, así no hay saltos de tiempo."));
    bypassAttachment = std::make_unique<ButtonAttachment> (proc.apvts, EQ::bypassId, bypassToggle);
    addAndMakeVisible (bypassToggle);
    autoGainToggle.setButtonText ("Igualar vol.");
    autoGainToggle.setTooltip (EQ::utf8 ("Compensa el volumen (loudness en 3 s) del procesado para que suene igual de fuerte que el original al comparar."));
    autoGainAttachment = std::make_unique<ButtonAttachment> (proc.apvts, EQ::autoGainId, autoGainToggle);
    addAndMakeVisible (autoGainToggle);

    // Los ajustes de la dinámica (umbral, ratio, ataque, release) se despliegan: la ventana crece al abrirlos.
    dynOpen = (bool) proc.apvts.state.getProperty ("dynOpen", false);
    dynExpandButton.setTooltip (EQ::utf8 ("Muestra u oculta los ajustes de la dinámica de cada banda. La ventana se agranda al desplegarlos."));
    dynExpandButton.onClick = [this] { setDynamicsOpen (! dynOpen); };
    addAndMakeVisible (dynExpandButton);

    applyBandColours();
    setDynamicsOpen (dynOpen);
    startTimerHz (10);
}

MedidoresEQAudioProcessorEditor::~MedidoresEQAudioProcessorEditor()
{
    stopTimer();
    for (auto& a : gainAttachments) a.reset();
    setLookAndFeel (nullptr);
}

void MedidoresEQAudioProcessorEditor::timerCallback()
{
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
    for (int i = 0; i < 4; ++i)
        slotButton[i].setToggleState (i == proc.getActiveSlot(), juce::dontSendNotification);
}

int MedidoresEQAudioProcessorEditor::windowHeight (bool dynamicsOpen)
{
    return dynamicsOpen ? 822 : 674;   // la ventana desplegada añade los ajustes de dinámica (148 px más que la fila compacta)
}

void MedidoresEQAudioProcessorEditor::setDynamicsOpen (bool open)
{
    dynOpen = open;
    proc.apvts.state.setProperty ("dynOpen", open, nullptr);
    dynExpandButton.setButtonText (open ? EQ::utf8 ("▾  Ocultar ajustes de dinámica") : EQ::utf8 ("▸  Ajustes de dinámica"));

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
        qk.label.setText (pultec ? EQ::utf8 ("ATEN") : "Q", juce::dontSendNotification);
        qk.slider.setTooltip (pultec ? EQ::utf8 ("Atenuación del Pultec, en la misma frecuencia que el realce. Doble clic: valor por defecto.")
                                     : EQ::utf8 ("Ancho de la banda: más Q, más estrecha. Doble clic: valor por defecto."));
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
    for (auto* s : { characterSwitch.get(), styleSwitch.get(), phaseSwitch.get(), ditherSwitch.get() })
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
    k.slider.setTooltip (tip + EQ::utf8 (" Doble clic: valor por defecto. Mayús + arrastrar: ajuste fino. Clic en el valor: escribir un número."));
    k.label.setText (text.toUpperCase(), juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.attachment = std::make_unique<SliderAttachment> (proc.apvts, id, k.slider);
    if (auto* p = proc.apvts.getParameter (id))
        k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    styleKnob (k);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

// Knob de ganancia con rango propio (±18/±12/±6/±3 según el ajuste "Rango").
void MedidoresEQAudioProcessorEditor::addGainKnob (Knob& k, int b, const juce::String& tip)
{
    k.capIndex = b;
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.slider.setTooltip (tip + EQ::utf8 (" Doble clic: 0 dB. Mayús + arrastrar: ajuste fino. Clic en el valor: escribir un número."));
    k.label.setText ("GAN", juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
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
    presetBox.addSectionHeading (EQ::utf8 ("Fábrica"));
    for (int i = 0; i < factoryNames.size(); ++i) presetBox.addItem (factoryNames[i], 1 + i);
    if (userNames.size() > 0)
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("Usuario");
        for (int i = 0; i < userNames.size(); ++i) presetBox.addItem (userNames[i], 1001 + i);
    }
    presetBox.setTextWhenNothingSelected (EQ::utf8 ("Presets…"));

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

void MedidoresEQAudioProcessorEditor::askPresetName()
{
    auto* w = new juce::AlertWindow ("Guardar preset", "Nombre del preset:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "", "");
    w->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

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
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Borrar preset",
                                        EQ::utf8 ("¿Borrar el preset \"") + name + "\"?", "Borrar", "Cancelar", this,
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
    presetBox.setBounds (bar.removeFromLeft (190));
    bar.removeFromLeft (6);
    saveButton.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (4);
    deleteButton.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (16);
    for (int i = 0; i < 4; ++i)
    {
        slotButton[i].setBounds (bar.removeFromLeft (30));
        bar.removeFromLeft (3);
    }
    bar.removeFromLeft (10);
    undoButton.setBounds (bar.removeFromLeft (76));
    bar.removeFromLeft (4);
    redoButton.setBounds (bar.removeFromLeft (72));

    rangeBox.setBounds (bar.removeFromRight (84));
    bar.removeFromRight (6);
    holdBox.setBounds (bar.removeFromRight (92));
    bar.removeFromRight (6);
    smoothBox.setBounds (bar.removeFromRight (110));
    bar.removeFromRight (6);
    resBox.setBounds (bar.removeFromRight (84));
    bar.removeFromRight (6);
    speedBox.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (6);
    analyzerBox.setBounds (bar.removeFromRight (92));
    area.removeFromTop (8);

    bezelRect = area.removeFromTop (186);
    curve.setBounds (bezelRect.reduced (10, 9));
    area.removeFromTop (8);

    stripRect = area.removeFromBottom (30);
    area.removeFromBottom (6);
    dynExpandButton.setBounds (stripRect.getCentreX() - 105, stripRect.getY() + 2, 210, 26);

    // Columnas (de izquierda a derecha): ENTRADA | FILTROS (paso alto arriba, paso bajo abajo) | Graves | Medio 1-4 | Agudos |
    //                                    CARÁCTER | MASTER | MEDICIÓN | SALIDA
    constexpr int numCols = 12;
    const int colW = area.getWidth() / numCols;
    const int x0 = area.getX();
    auto colX = [&] (int col) { return x0 + col * colW; };
    const int bandCol[EQ::NumBands] = { 1, 2, 3, 4, 5, 6, 7, 1 };   // paso alto y paso bajo comparten columna

    auto toggleRow = area.removeFromTop (26);
    auto comboRow = area.removeFromBottom (58);
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
            slopeButtons[b]->setBounds (x + 8, blockTop + 34 + knobH + 8, colW - 16, 24);
            continue;
        }

        toggles[b].setBounds (x + 8, toggleRow.getY() + 2, colW - 12, toggleRow.getHeight());
        place (knobs[b][0], { x, area.getY(), colW, rowH });
        place (knobs[b][1], { x, area.getY() + rowH, colW, rowH });
        place (knobs[b][2], { x, area.getY() + 2 * rowH, colW, rowH });
        soloButton[b].setBounds (x + colW - 26, area.getY(), 22, 15);

        dynToggle[b].setBounds (x + 8, dynRow.getY() + 2, colW - 12, 26);
        dynMeter[b]->setBounds (x + 10, dynRow.getY() + 30, colW - 20, 16);
        const int knobRowH = 60;
        place (thrKnob[b],     { x,            dynRow.getY() + 50,  colW / 2, knobRowH });
        place (ratioKnob[b],   { x + colW / 2, dynRow.getY() + 50,  colW / 2, knobRowH });
        place (attackKnob[b],  { x,            dynRow.getY() + 50 + knobRowH, colW / 2, knobRowH });
        place (releaseKnob[b], { x + colW / 2, dynRow.getY() + 50 + knobRowH, colW / 2, knobRowH });
        dmodeButton[b]->setBounds (x + 8, dynRow.getY() + 50 + 2 * knobRowH + 4, colW - 16, 22);

        typeButton[b]->setBounds (x + 8, comboRow.getY() + 4, colW - 16, 24);
        placementButtons[b]->setBounds (x + 4, comboRow.getY() + 32, colW - 8, 24);
    }

    // Columnas de los extremos, de carácter y de master
    const int inCol = colX (0), characterCol = colX (8), masterCol = colX (9), meterCol = colX (10), outCol = colX (11);
    const int bottomY = colBottom - 4;
    const int y3 = area.getY() + 2 * rowH, rest = comboRow.getY() - y3;

    place (driveKnob, { characterCol, area.getY(), colW, rowH });
    place (mixKnob,   { characterCol, area.getY() + rowH, colW, rowH });
    characterSwitch->setBounds (characterCol + 2, y3, colW - 4, rest / 2);
    styleSwitch->setBounds (characterCol + 2, y3 + rest / 2, colW - 4, rest / 2);
    osButton->setBounds (characterCol + 6, comboRow.getY() + 4, colW - 12, 24);
    gainRangeButton->setBounds (characterCol + 6, comboRow.getY() + 32, colW - 12, 24);

    place (monoKnob,  { masterCol, area.getY(), colW, rowH });
    place (widthKnob, { masterCol, area.getY() + rowH, colW, rowH });
    phaseSwitch->setBounds (masterCol + 2, y3, colW - 4, rest / 2);
    ditherSwitch->setBounds (masterCol + 2, y3 + rest / 2, colW - 4, rest / 2);
    scButton->setBounds (masterCol + 6, comboRow.getY() + 4, colW - 12, 24);
    detButton->setBounds (masterCol + 6, comboRow.getY() + 32, colW - 12, 24);

    meterPanel.setBounds (meterCol + 2, area.getY() + 2, colW - 4, bottomY - area.getY() - 2);

    place (inKnob,  { inCol,  area.getY(), colW, rowH });
    place (outKnob, { outCol, area.getY(), colW, rowH });
    const int vuY = area.getY() + rowH + 4;
    inVU.setBounds  (inCol  + 4, vuY, colW - 8, comboRow.getY() - vuY - 4);
    outVU.setBounds (outCol + 4, vuY, colW - 8, comboRow.getY() - vuY - 4);
    bypassToggle.setBounds (inCol + 8, comboRow.getY() + 16, colW - 12, 26);
    autoGainToggle.setBounds (outCol + 8, comboRow.getY() + 16, colW - 12, 26);

    // Recuadros serigrafiados: de la fila de interruptores al último control
    sections.clear();
    const int panelTop = colTop - 3, panelBottom = colBottom + 3;
    auto addSection = [&] (int col, const juce::String& title)
    {
        sections.push_back ({ { colX (col) + 2, panelTop, colW - 4, panelBottom - panelTop }, title });
    };
    addSection (0, "ENTRADA");
    addSection (1, {});
    for (int col = 2; col <= 7; ++col) addSection (col, {});
    addSection (8, EQ::utf8 ("CARÁCTER"));
    addSection (9, "MASTER");
    addSection (10, EQ::utf8 ("MEDICIÓN"));
    addSection (11, "SALIDA");
    filterSplitY = colTop + filterBlockH + 2;
    sectionTitleY = toggleRow.getY() + 2;
}
