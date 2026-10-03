#include "Widgets.h"

//==============================================================================
SegmentedButtons::SegmentedButtons (juce::RangedAudioParameter& param, const juce::StringArray& l, juce::Colour c)
    : labels (l), lamp (c),
      attachment (param, [this] (float v) { current = juce::jlimit (0, labels.size() - 1, juce::roundToInt (v)); repaint(); })
{
    attachment.sendInitialUpdate();
}

void SegmentedButtons::select (int index)
{
    attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, labels.size() - 1, index));
}

void SegmentedButtons::mouseDown (const juce::MouseEvent& e)
{
    const int n = juce::jmax (1, labels.size());
    select ((int) (e.position.x / ((float) getWidth() / (float) n)));
}

void SegmentedButtons::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (w.deltaY != 0.0f) select (current + (w.deltaY > 0.0f ? -1 : 1));
}

void SegmentedButtons::paint (juce::Graphics& g)
{
    const auto& P = paletteOf (*this);
    const int n = juce::jmax (1, labels.size());
    const float segW = (float) getWidth() / (float) n;

    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * segW, 0.0f, segW, (float) getHeight()).reduced (1.5f, 1.0f);
        const bool on = i == current;

        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.2f), 3.0f);
        const auto top = on ? P.control.darker (0.35f) : P.control.brighter (0.25f);
        const auto bottom = on ? P.control.darker (0.15f) : P.control.darker (0.25f);
        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        if (on)   // piloto encendido en el borde superior
        {
            g.setColour (lamp.withAlpha (0.35f));
            g.fillRoundedRectangle (r.getX() + 3.0f, r.getY() + 1.5f, r.getWidth() - 6.0f, 4.0f, 2.0f);
            g.setColour (lamp);
            g.fillRoundedRectangle (r.getX() + 4.0f, r.getY() + 2.5f, r.getWidth() - 8.0f, 2.0f, 1.0f);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawLine (r.getX() + 3.0f, r.getY() + 1.5f, r.getRight() - 3.0f, r.getY() + 1.5f, 1.0f);
        }

        g.setColour (on ? P.ink : P.ink.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (12.0f, on ? juce::Font::bold : juce::Font::plain)));
        g.drawText (labels[i], r.withTrimmedTop (on ? 3.0f : 1.0f), juce::Justification::centred, true);
    }
}

//==============================================================================
RotarySwitch::RotarySwitch (juce::RangedAudioParameter& param, const juce::String& t, const juce::StringArray& l, juce::Colour c)
    : title (t), labels (l), cap (c),
      attachment (param, [this] (float v) { current = juce::jlimit (0, labels.size() - 1, juce::roundToInt (v)); repaint(); })
{
    attachment.sendInitialUpdate();
}

float RotarySwitch::angleFor (int index) const
{
    const int n = juce::jmax (2, labels.size());
    return startAngle + (endAngle - startAngle) * (float) index / (float) (n - 1);
}

void RotarySwitch::select (int index)
{
    attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, labels.size() - 1, index));
}

// Geometría del selector: el grupo (knob + etiquetas) va pegado al título, no centrado en un componente alto.
RotarySwitch::Geometry RotarySwitch::geometry() const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    Geometry g;
    g.r = juce::jmin (w * 0.2f, (h - 16.0f) * 0.2f);
    g.lx = juce::jmin (w * 0.40f, g.r * 2.4f);
    g.ly = juce::jmin ((h - 16.0f) * 0.42f, g.r * 2.1f);
    g.c = { w * 0.5f, juce::jmin (h * 0.58f, 16.0f + g.ly + 14.0f) };
    return g;
}

void RotarySwitch::setFromPoint (juce::Point<float> p)
{
    const auto c = geometry().c;
    const float angle = juce::jlimit (startAngle, endAngle, std::atan2 (p.x - c.x, -(p.y - c.y)));
    const int n = juce::jmax (2, labels.size());
    select (juce::roundToInt ((angle - startAngle) / (endAngle - startAngle) * (float) (n - 1)));
}

void RotarySwitch::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (w.deltaY != 0.0f) select (current + (w.deltaY > 0.0f ? -1 : 1));
}

void RotarySwitch::paint (juce::Graphics& g)
{
    const auto& P = paletteOf (*this);

    g.setColour (P.inkMuted);
    g.setFont (juce::Font (juce::FontOptions (11.5f)).withExtraKerningFactor (0.08f));
    g.drawText (title, 0, 0, getWidth(), 16, juce::Justification::centred);

    const auto geo = geometry();
    const auto c = geo.c;
    const float r = geo.r, lx = geo.lx, ly = geo.ly;   // lx, ly: elipse donde van las posiciones impresas

    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (int i = 0; i < labels.size(); ++i)
    {
        const float a = angleFor (i);
        const juce::Point<float> p (c.x + std::sin (a) * lx, c.y - std::cos (a) * ly);
        g.setColour (P.ink.withAlpha (i == current ? 1.0f : 0.65f));
        g.drawText (labels[i], juce::Rectangle<float> (p.x - 22.0f, p.y - 6.0f, 44.0f, 12.0f), juce::Justification::centred, false);

        // marca impresa entre la etiqueta y el knob
        const float t0 = r + 4.0f, t1 = r + 8.0f;
        g.setColour (P.ink.withAlpha (0.7f));
        g.drawLine (c.x + std::sin (a) * t0, c.y - std::cos (a) * t0, c.x + std::sin (a) * t1, c.y - std::cos (a) * t1, 1.4f);
    }

    // Knob
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillEllipse (c.x - r - (float) i + 1.0f, c.y - r - (float) i + 2.5f, (r + (float) i) * 2.0f, (r + (float) i) * 2.0f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8d8f90), c.x - r * 0.6f, c.y - r * 0.8f,
                                             juce::Colour (0xff2d2e30), c.x + r * 0.7f, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);

    const float capR = r * 0.74f;
    g.setGradientFill (juce::ColourGradient (cap.brighter (0.45f), c.x - capR * 0.5f, c.y - capR * 0.7f,
                                             cap.darker (0.55f), c.x + capR * 0.6f, c.y + capR * 0.9f, false));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

    // Pico indicador (flecha) que apunta a la posición activa
    juce::Path pointer;
    pointer.addTriangle (-3.2f, -capR * 0.3f, 3.2f, -capR * 0.3f, 0.0f, -r - 2.5f);
    const bool lightCap = cap.getPerceivedBrightness() > 0.55f;
    g.setColour (lightCap ? juce::Colour (0xff1c1a16) : juce::Colour (0xfff5efe0));
    g.fillPath (pointer, juce::AffineTransform::rotation (angleFor (current)).translated (c.x, c.y));
}

//==============================================================================
VUPair::VUPair (MedidoresEQAudioProcessor& p, bool isInput) : proc (p), input (isInput)
{
    setTooltip (juce::String::fromUTF8 ("Medidor VU de aguja (L arriba, R abajo). El piloto rojo avisa de picos de 0 dBFS. Clic: borrar el pico máximo."));
    startTimerHz (30);
}

void VUPair::timerCallback()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = input ? proc.takeInputPeak (ch) : proc.takeOutputPeak (ch);
        level[ch] += (peak - level[ch]) * 0.105f;   // constante de integración ~300 ms, como un VU
        if (peak >= 0.989f) ledFrames[ch] = 45;
        else if (ledFrames[ch] > 0) --ledFrames[ch];
        held = juce::jmax (held, juce::Decibels::gainToDecibels (peak, -100.0f));
    }
    repaint();
}

void VUPair::drawFace (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& tag, float lvl, bool led)
{
    const auto& P = paletteOf (*this);

    // Marco y cara retroiluminada
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.expanded (2.5f).translated (0.0f, 1.5f), 6.0f);
    g.setColour (P.ear.darker (0.4f));
    g.fillRoundedRectangle (r.expanded (2.5f), 6.0f);
    g.setGradientFill (juce::ColourGradient (P.vuFace.brighter (0.12f), r.getCentreX(), r.getBottom(),
                                             P.vuFace.darker (0.45f), r.getCentreX() - r.getWidth() * 0.9f, r.getY(), true));
    g.fillRoundedRectangle (r, 4.0f);

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());

    constexpr float vmin = -40.0f, vmax = 3.0f, span = 0.66f;
    const float rad = juce::jmin (1.04f * r.getHeight(), (r.getWidth() * 0.5f - 8.0f) / std::sin (span));
    const juce::Point<float> pivot (r.getCentreX(), r.getY() + 0.30f * r.getHeight() + rad);
    auto pointAt = [&] (float db, float radius)
    {
        const float a = -span + 2.0f * span * (juce::jlimit (vmin - 2.0f, vmax + 2.0f, db) - vmin) / (vmax - vmin);
        return juce::Point<float> (pivot.x + std::sin (a) * radius, pivot.y - std::cos (a) * radius);
    };

    // Escala: arco, zona roja, marcas y números
    juce::Path arc, red;
    for (float db = vmin; db <= vmax + 0.01f; db += 0.5f)
    {
        const auto p = pointAt (db, rad);
        if (db == vmin) arc.startNewSubPath (p); else arc.lineTo (p);
    }
    for (float db = -3.0f; db <= vmax + 0.01f; db += 0.5f)
    {
        const auto p = pointAt (db, rad + 1.5f);
        if (db == -3.0f) red.startNewSubPath (p); else red.lineTo (p);
    }
    g.setColour (P.vuInk.withAlpha (0.85f));
    g.strokePath (arc, juce::PathStrokeType (1.1f));
    g.setColour (P.vuRed);
    g.strokePath (red, juce::PathStrokeType (3.0f));

    g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
    for (float db : { -40.0f, -35.0f, -30.0f, -25.0f, -20.0f, -15.0f, -10.0f, -5.0f, 0.0f, 3.0f })
    {
        const bool major = db == -40.0f || db == -30.0f || db == -20.0f || db == -10.0f || db == 0.0f || db == 3.0f || db == -5.0f;
        const auto a = pointAt (db, rad), b = pointAt (db, rad - (major ? 6.0f : 3.5f));
        g.setColour (db >= -3.0f ? P.vuRed : P.vuInk.withAlpha (0.85f));
        g.drawLine (a.x, a.y, b.x, b.y, major ? 1.3f : 0.9f);
        if (db == -40.0f || db == -20.0f || db == -10.0f || db == 0.0f)   // solo estos llevan número, para que no se pisen
        {
            const auto t = pointAt (db, rad - 14.0f);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (t.x - 11.0f, t.y - 5.0f, 22.0f, 10.0f), juce::Justification::centred, false);
        }
    }

    // Aguja
    const float db = juce::Decibels::gainToDecibels (lvl, -80.0f);
    const auto tip = pointAt (db, rad - 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawLine (pivot.x + 1.5f, pivot.y + 1.5f, tip.x + 1.5f, tip.y + 1.5f, 1.6f);
    g.setColour (P.vuInk.darker (0.4f));
    g.drawLine (pivot.x, pivot.y, tip.x, tip.y, 1.5f);
    if (pivot.y < r.getBottom() + 4.0f)   // cubo del eje si queda dentro de la cara
    {
        g.setColour (P.vuInk.darker (0.2f));
        g.fillEllipse (pivot.x - 4.0f, pivot.y - 4.0f, 8.0f, 8.0f);
    }

    // Cristal: reflejo en la mitad superior
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.30f), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.5f, r.getY() + r.getHeight() * 0.6f, false));
    g.fillRect (r.withHeight (r.getHeight() * 0.6f));
    g.setColour (juce::Colours::black.withAlpha (0.35f));   // sombra del borde superior
    g.fillRect (r.withHeight (2.5f));
    g.restoreState();

    // Etiqueta de canal y piloto de pico
    g.setColour (P.vuInk.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (tag, juce::Rectangle<float> (r.getX() + 5.0f, r.getY() + 3.0f, 16.0f, 12.0f), juce::Justification::centredLeft, false);

    const juce::Point<float> lamp (r.getRight() - 9.0f, r.getY() + 9.0f);
    if (led) { g.setColour (P.vuRed.withAlpha (0.35f)); g.fillEllipse (lamp.x - 6.0f, lamp.y - 6.0f, 12.0f, 12.0f); }
    g.setGradientFill (juce::ColourGradient (led ? juce::Colour (0xffff7a66) : juce::Colour (0xff4a2a24), lamp.x - 1.0f, lamp.y - 1.5f,
                                             led ? P.vuRed.darker (0.3f) : juce::Colour (0xff25130f), lamp.x + 2.0f, lamp.y + 2.5f, true));
    g.fillEllipse (lamp.x - 3.5f, lamp.y - 3.5f, 7.0f, 7.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (lamp.x - 3.5f, lamp.y - 3.5f, 7.0f, 7.0f, 0.8f);
}

void VUPair::paint (juce::Graphics& g)
{
    const auto& P = paletteOf (*this);
    auto area = getLocalBounds().toFloat();

    const float gap = 8.0f, readoutH = 22.0f, below = 6.0f;
    const float faceH = juce::jmin (112.0f, (area.getHeight() - gap - readoutH - below) * 0.5f);
    auto faces = area.withTrimmedLeft (3.0f).withTrimmedRight (3.0f).withHeight (faceH * 2.0f + gap);
    drawFace (g, faces.removeFromTop (faceH), "L", level[0], ledFrames[0] > 0);
    faces.removeFromTop (gap);
    drawFace (g, faces.removeFromTop (faceH), "R", level[1], ledFrames[1] > 0);
    const juce::Rectangle<float> readout (area.getX(), area.getY() + faceH * 2.0f + gap + below, area.getWidth(), readoutH);

    // Pico máximo en una ventana empotrada
    const auto win = readout.reduced (6.0f, 2.0f);
    g.setColour (P.inset);
    g.fillRoundedRectangle (win, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (win, 3.0f, 1.0f);
    g.setColour (held > -0.1f ? P.vuRed.brighter (0.4f) : P.insetText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (held > -99.0f ? juce::String (held, 1) + " dB" : juce::String ("--"), win, juce::Justification::centred);
}

//==============================================================================
juce::Image makePlate (int width, int height, const Palette& p)
{
    const int w = juce::jmax (1, width), h = juce::jmax (1, height);
    juce::Image img (juce::Image::RGB, w, h, false);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    juce::Random rng (7);
    const float cx = 0.5f * (float) w, cy = 0.5f * (float) h;

    for (int y = 0; y < h; ++y)
    {
        const auto base = p.plateTop.interpolatedWith (p.plateBottom, (float) y / (float) juce::jmax (1, h - 1));
        const float streak = (rng.nextFloat() - 0.5f) * p.grain * 0.9f;   // vetas horizontales del cepillado
        const float dy = ((float) y - cy) / cy;
        for (int x = 0; x < w; ++x)
        {
            const float dx = ((float) x - cx) / cx;
            const float vignette = 1.0f - 0.16f * juce::jlimit (0.0f, 1.0f, (dx * dx + dy * dy) * 0.5f);
            const float n = (1.0f + streak + (rng.nextFloat() - 0.5f) * p.grain) * vignette;
            bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, base.getFloatRed() * n),
                                                                  juce::jlimit (0.0f, 1.0f, base.getFloatGreen() * n),
                                                                  juce::jlimit (0.0f, 1.0f, base.getFloatBlue() * n), 1.0f));
        }
    }
    return img;
}

void drawScrew (juce::Graphics& g, juce::Point<float> c, float r, float slotAngle)
{
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - r + 0.5f, c.y - r + 1.5f, r * 2.0f, r * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd2d2cf), c.x - r * 0.5f, c.y - r * 0.6f,
                                             juce::Colour (0xff55565a), c.x + r * 0.7f, c.y + r * 0.8f, false));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.9f);

    const float dx = std::cos (slotAngle) * r * 0.75f, dy = std::sin (slotAngle) * r * 0.75f;
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawLine (c.x - dx, c.y - dy, c.x + dx, c.y + dy, juce::jmax (1.2f, r * 0.28f));
}

//==============================================================================
RangedSliderAttachment::RangedSliderAttachment (juce::RangedAudioParameter& p, juce::Slider& s)
    : param (p), slider (s),
      attachment (p, [this] (float v) { updating = true; slider.setValue ((double) v, juce::dontSendNotification); updating = false; })
{
    slider.textFromValueFunction = [&p] (double v) { return p.getText (p.convertTo0to1 ((float) v), 0); };
    slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    slider.onDragStart = [this] { dragging = true; attachment.beginGesture(); };
    slider.onDragEnd   = [this] { attachment.endGesture(); dragging = false; };
    slider.onValueChange = [this]
    {
        if (updating) return;
        if (dragging) attachment.setValueAsPartOfGesture ((float) slider.getValue());
        else          attachment.setValueAsCompleteGesture ((float) slider.getValue());   // valor escrito o rueda
    };
    slider.setDoubleClickReturnValue (true, (double) p.convertFrom0to1 (p.getDefaultValue()));
    setLimit (limit);
}

RangedSliderAttachment::~RangedSliderAttachment()
{
    slider.onValueChange = nullptr;
    slider.onDragStart = nullptr;
    slider.onDragEnd = nullptr;
    slider.textFromValueFunction = nullptr;
    slider.valueFromTextFunction = nullptr;
}

void RangedSliderAttachment::setLimit (double newLimit)
{
    limit = newLimit;
    updating = true;
    slider.setRange (-limit, limit, 0.1);
    slider.setValue ((double) param.convertFrom0to1 (param.getValue()), juce::dontSendNotification);
    updating = false;
    slider.repaint();
}

//==============================================================================
CycleButton::CycleButton (juce::RangedAudioParameter& param, const juce::StringArray& l, const juce::String& pre)
    : labels (l), prefix (pre),
      attachment (param, [this] (float v)
      {
          current = juce::jlimit (0, juce::jmax (0, labels.size() - 1), juce::roundToInt (v));
          setButtonText (prefix + labels[current]);
          if (onChanged) onChanged (current);
      })
{
    onClick = [this] { attachment.setValueAsCompleteGesture ((float) ((current + 1) % juce::jmax (1, labels.size()))); };
    attachment.sendInitialUpdate();
}

//==============================================================================
MeterPanel::MeterPanel (MedidoresEQAudioProcessor& p) : proc (p)
{
    setTooltip (juce::String::fromUTF8 ("Goniómetro y correlación estéreo (+1 mono, 0 muy ancho, -1 en contrafase: peligro en mono) "
                                        "y loudness de la salida: M momentáneo, S corto plazo, I integrado, TP true peak. Clic: borrar."));
    points.reserve (maxPoints * 2);
    startTimerHz (30);
}

void MeterPanel::timerCallback()
{
    float tmp[512 * 2];
    while (const int n = proc.pullGoniometer (tmp, 512))
        for (int i = 0; i < n; ++i)
        {
            const float l = tmp[i * 2], r = tmp[i * 2 + 1];
            points.push_back ((l - r) * 0.70710678f);   // x = Side
            points.push_back ((l + r) * 0.70710678f);   // y = Mid
        }
    if ((int) points.size() > maxPoints * 2)
        points.erase (points.begin(), points.begin() + ((int) points.size() - maxPoints * 2));

    correlation = proc.getCorrelation();
    smoothCorr += (correlation - smoothCorr) * 0.25f;
    repaint();
}

void MeterPanel::paint (juce::Graphics& g)
{
    const auto& P = paletteOf (*this);
    auto area = getLocalBounds().toFloat().reduced (4.0f, 0.0f);

    // Goniómetro
    const float side = juce::jmin (area.getWidth(), area.getHeight() * 0.5f);
    const auto scope = juce::Rectangle<float> (side, side).withCentre ({ area.getCentreX(), area.getY() + side * 0.5f });
    g.setColour (P.screen);
    g.fillRoundedRectangle (scope, 6.0f);
    g.setColour (P.ink.withAlpha (0.12f));
    g.drawLine (scope.getX() + 4.0f, scope.getCentreY(), scope.getRight() - 4.0f, scope.getCentreY(), 1.0f);
    g.drawLine (scope.getCentreX(), scope.getY() + 4.0f, scope.getCentreX(), scope.getBottom() - 4.0f, 1.0f);
    g.drawLine (scope.getX() + 8.0f, scope.getBottom() - 8.0f, scope.getRight() - 8.0f, scope.getY() + 8.0f, 0.7f);
    g.drawLine (scope.getX() + 8.0f, scope.getY() + 8.0f, scope.getRight() - 8.0f, scope.getBottom() - 8.0f, 0.7f);
    g.setColour (P.inkMuted);
    g.setFont (9.5f);
    g.drawText ("M", scope.withHeight (12.0f).translated (0.0f, 1.0f), juce::Justification::centred, false);
    g.drawText ("L", scope.getX() + 4.0f, scope.getY() + 3.0f, 12.0f, 12.0f, juce::Justification::left, false);
    g.drawText ("R", scope.getRight() - 16.0f, scope.getY() + 3.0f, 12.0f, 12.0f, juce::Justification::right, false);

    const float k = side * 0.5f * 0.92f;
    const int count = (int) points.size() / 2;
    for (int i = 0; i < count; ++i)
    {
        const float x = juce::jlimit (-1.0f, 1.0f, points[(size_t) i * 2] * 1.8f);
        const float y = juce::jlimit (-1.0f, 1.0f, points[(size_t) i * 2 + 1] * 1.8f);
        g.setColour (P.trace.withAlpha (0.10f + 0.5f * (float) i / (float) juce::jmax (1, count)));
        g.fillRect (scope.getCentreX() + x * k - 0.7f, scope.getCentreY() - y * k - 0.7f, 1.4f, 1.4f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (scope, 6.0f, 1.2f);

    // Correlación: -1 .. +1
    auto bar = juce::Rectangle<float> (area.getX() + 2.0f, scope.getBottom() + 12.0f, area.getWidth() - 4.0f, 12.0f);
    g.setColour (P.inset);
    g.fillRoundedRectangle (bar, 3.0f);
    const float cx = bar.getCentreX();
    const float px = bar.getX() + bar.getWidth() * (smoothCorr * 0.5f + 0.5f);
    g.setColour ((smoothCorr < 0.0f ? P.vuRed : P.trace).withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (juce::jmin (cx, px), bar.getY() + 2.0f, std::abs (px - cx), bar.getHeight() - 4.0f));
    g.setColour (P.ink.withAlpha (0.6f));
    g.drawLine (cx, bar.getY(), cx, bar.getBottom(), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (bar, 3.0f, 1.0f);
    g.setColour (P.inkMuted);
    g.setFont (9.5f);
    g.drawText ("-1", bar.getX(), bar.getBottom() + 1.0f, 20.0f, 11.0f, juce::Justification::left, false);
    g.drawText (juce::String (smoothCorr, 2), bar.getCentreX() - 22.0f, bar.getBottom() + 1.0f, 44.0f, 11.0f, juce::Justification::centred, false);
    g.drawText ("+1", bar.getRight() - 20.0f, bar.getBottom() + 1.0f, 20.0f, 11.0f, juce::Justification::right, false);

    // Loudness
    const auto l = proc.getLoudness();
    auto text = [] (float v) { return v < -150.0f ? juce::String ("--") : juce::String (v, 1); };
    struct Row { const char* tag; juce::String value; bool hot; };
    const Row rows[] = { { "M", text (l.momentary), false }, { "S", text (l.shortTerm), false },
                         { "I", text (l.integrated), false }, { "TP", text (l.truePeakDb), l.truePeakDb > -1.0f } };
    float y = bar.getBottom() + 22.0f;
    const float rowH = juce::jmin (24.0f, (area.getBottom() - y - 2.0f) / 5.0f);
    for (const auto& r : rows)
    {
        const auto win = juce::Rectangle<float> (area.getX() + 2.0f, y, area.getWidth() - 4.0f, rowH - 2.0f);
        g.setColour (P.inset);
        g.fillRoundedRectangle (win, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (win, 3.0f, 1.0f);
        g.setColour (P.inkMuted);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (r.tag, win.reduced (5.0f, 0.0f), juce::Justification::centredLeft, false);
        g.setColour (r.hot ? P.vuRed.brighter (0.4f) : P.insetText);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (r.value, win.reduced (5.0f, 0.0f), juce::Justification::centredRight, false);
        y += rowH;
    }
    g.setColour (P.inkMuted);
    g.setFont (9.0f);
    g.drawText ("LUFS  /  dBTP", juce::Rectangle<float> (area.getX(), y, area.getWidth(), 12.0f), juce::Justification::centred, false);
}
