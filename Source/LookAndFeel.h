#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// Estilo de hardware analógico: knobs con capuchón de color y corona de marcas, interruptores de palanca,
// pulsadores biselados y ventanas empotradas para los valores. Toda la paleta sale del tema activo.
class EQLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EQLookAndFeel() { setDefaultSansSerifTypefaceName ("Futura"); setPalette (Themes::get (0)); }

    const Palette& getPalette() const { return *palette; }

    void setPalette (const Palette& p)
    {
        palette = &p;
        setColour (juce::ResizableWindow::backgroundColourId, p.plateBottom);
        setColour (juce::Label::textColourId, p.inkMuted);
        setColour (juce::Slider::textBoxTextColourId, p.insetText);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, p.inset);
        setColour (juce::ComboBox::backgroundColourId, p.inset);
        setColour (juce::ComboBox::textColourId, p.insetText);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::black.withAlpha (0.6f));
        setColour (juce::ComboBox::arrowColourId, p.accent);
        setColour (juce::PopupMenu::backgroundColourId, p.inset);
        setColour (juce::PopupMenu::textColourId, p.insetText);
        setColour (juce::PopupMenu::headerTextColourId, p.inkMuted);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.control);
        setColour (juce::PopupMenu::highlightedTextColourId, p.ink);
        setColour (juce::TextButton::buttonColourId, p.control);
        setColour (juce::TextButton::textColourOffId, p.ink);
        setColour (juce::TextButton::textColourOnId, p.ink);
        setColour (juce::ToggleButton::textColourId, p.ink);
        setColour (juce::TooltipWindow::backgroundColourId, p.inset);
        setColour (juce::TooltipWindow::textColourId, p.insetText);
        setColour (juce::TooltipWindow::outlineColourId, p.line.withAlpha (0.4f));
        setColour (juce::AlertWindow::backgroundColourId, p.inset);
        setColour (juce::AlertWindow::textColourId, p.ink);
        setColour (juce::TextEditor::backgroundColourId, p.screen);
        setColour (juce::TextEditor::textColourId, p.insetText);
        setColour (juce::TextEditor::outlineColourId, p.line.withAlpha (0.4f));
    }

    // Etiquetas pequeñas, en mayúsculas y con algo de espaciado, como la serigrafía de un equipo.
    juce::Font getLabelFont (juce::Label&) override        { return juce::Font (juce::FontOptions (11.5f)).withExtraKerningFactor (0.08f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override  { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (12.5f)); }
    juce::Font getPopupMenuFont() override                 { return juce::Font (juce::FontOptions (14.0f)); }

    //==========================================================================
    // Knob de hardware: corona de marcas serigrafiadas, falda metálica con moleteado, capuchón del color de la banda y aguja.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto& P = *palette;
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto c = bounds.getCentre();
        const auto cap = s.findColour (juce::Slider::rotarySliderFillColourId);
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;

        // Marcas impresas en la chapa
        constexpr int numTicks = 13;
        const float tickOuter = radius, tickInner = radius - juce::jmax (3.0f, radius * 0.13f);
        for (int i = 0; i < numTicks; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (numTicks - 1);
            const bool major = (i % 6 == 0) || (bipolar && i == numTicks / 2);
            const float inner = major ? tickInner - 2.0f : tickInner;
            g.setColour (P.ink.withAlpha (major ? 0.95f : 0.6f));
            g.drawLine (c.x + std::sin (a) * inner, c.y - std::cos (a) * inner,
                        c.x + std::sin (a) * tickOuter, c.y - std::cos (a) * tickOuter, major ? 1.8f : 1.2f);
        }

        const float skirtR = tickInner - 3.0f;
        if (skirtR < 5.0f) return;

        // Sombra de contacto
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.fillEllipse (c.x - skirtR - (float) i + 1.5f, c.y - skirtR - (float) i + 3.0f, (skirtR + (float) i) * 2.0f, (skirtR + (float) i) * 2.0f);
        }

        // Falda metálica
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8d8f90), c.x - skirtR * 0.6f, c.y - skirtR * 0.8f,
                                                 juce::Colour (0xff2d2e30), c.x + skirtR * 0.7f, c.y + skirtR, false));
        g.fillEllipse (c.x - skirtR, c.y - skirtR, skirtR * 2.0f, skirtR * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (c.x - skirtR, c.y - skirtR, skirtR * 2.0f, skirtR * 2.0f, 1.0f);

        // Moleteado (solo si el knob es lo bastante grande)
        if (skirtR > 14.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            for (int i = 0; i < 36; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
                g.drawLine (c.x + std::sin (a) * (skirtR - 3.0f), c.y - std::cos (a) * (skirtR - 3.0f),
                            c.x + std::sin (a) * (skirtR - 0.5f), c.y - std::cos (a) * (skirtR - 0.5f), 0.8f);
            }
        }

        // Capuchón de color
        const float capR = skirtR * 0.74f;
        g.setGradientFill (juce::ColourGradient (cap.brighter (0.45f), c.x - capR * 0.5f, c.y - capR * 0.7f,
                                                 cap.darker (0.55f), c.x + capR * 0.6f, c.y + capR * 0.9f, false));
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

        // Brillo especular
        juce::Path shine;
        shine.addCentredArc (c.x, c.y, capR * 0.78f, capR * 0.78f, 0.0f, -2.5f, -1.1f, true);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.strokePath (shine, juce::PathStrokeType (juce::jmax (1.2f, capR * 0.12f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Aguja: oscura sobre capuchones claros y clara sobre los oscuros
        const bool lightCap = cap.getPerceivedBrightness() > 0.55f;
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.4f, -capR + 1.5f, 2.8f, capR * 0.82f, 1.4f);
        g.setColour (lightCap ? juce::Colour (0xff1c1a16) : juce::Colour (0xfff5efe0));
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    //==========================================================================
    // Interruptor de palanca: placa con la palanca hacia arriba (activado) o hacia abajo, y la punta encendida en el color de la banda.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        const auto& P = *palette;
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        const auto tip = b.findColour (juce::ToggleButton::tickColourId);

        const juce::Rectangle<float> plate (r.getX() + 2.0f, r.getCentreY() - 12.0f, 20.0f, 24.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (plate.translated (0.0f, 1.5f), 4.0f);
        g.setGradientFill (juce::ColourGradient (P.inset.brighter (0.15f), plate.getX(), plate.getY(), P.inset.darker (0.3f), plate.getX(), plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (plate, 4.0f, 1.0f);

        const auto pivot = plate.getCentre();
        const juce::Point<float> end (pivot.x, pivot.y + (on ? -8.0f : 8.0f));
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawLine (pivot.x + 0.8f, pivot.y + 1.0f, end.x + 0.8f, end.y + 1.5f, 5.0f);
        g.setColour (juce::Colour (0xffc9c9c6));
        g.drawLine (pivot.x, pivot.y, end.x, end.y, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawLine (pivot.x - 0.8f, pivot.y, end.x - 0.8f, end.y, 1.2f);

        if (on)   // la punta se enciende
        {
            g.setColour (tip.withAlpha (0.28f));
            g.fillEllipse (end.x - 6.0f, end.y - 6.0f, 12.0f, 12.0f);
        }
        g.setGradientFill (juce::ColourGradient (on ? tip.brighter (0.5f) : P.lampOff.brighter (0.4f), end.x - 1.0f, end.y - 1.5f,
                                                 on ? tip.darker (0.3f) : P.lampOff.darker (0.4f), end.x + 2.0f, end.y + 2.5f, true));
        g.fillEllipse (end.x - 3.2f, end.y - 3.2f, 6.4f, 6.4f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (end.x - 3.2f, end.y - 3.2f, 6.4f, 6.4f, 0.8f);

        if (highlighted)
        {
            g.setColour (P.ink.withAlpha (0.35f));
            g.drawRoundedRectangle (plate.expanded (1.5f), 5.0f, 1.0f);
        }

        g.setColour (P.ink.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (plate.getWidth() + 10.0f), juce::Justification::centredLeft, true);
    }

    // Pulsador biselado; los que conmutan llevan un piloto que se enciende.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) override
    {
        const auto& P = *palette;
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        const bool pressed = down || on;

        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 4.0f);

        const auto top = pressed ? P.control.darker (0.25f) : P.control.brighter (0.25f);
        const auto bottom = pressed ? P.control.darker (0.1f) : P.control.darker (0.25f);
        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (pressed ? 0.04f : 0.14f));
        g.drawLine (r.getX() + 4.0f, r.getY() + 1.5f, r.getRight() - 4.0f, r.getY() + 1.5f, 1.0f);
        if (highlighted)
        {
            g.setColour (P.ink.withAlpha (0.18f));
            g.fillRoundedRectangle (r, 4.0f);
        }

        if (b.getClickingTogglesState())   // piloto
        {
            const auto colour = b.findColour (juce::TextButton::buttonOnColourId);
            const juce::Point<float> lamp (r.getX() + 11.0f, r.getCentreY());
            if (on) { g.setColour (colour.withAlpha (0.3f)); g.fillEllipse (lamp.x - 6.0f, lamp.y - 6.0f, 12.0f, 12.0f); }
            g.setGradientFill (juce::ColourGradient (on ? colour.brighter (0.5f) : P.lampOff.brighter (0.4f), lamp.x - 1.0f, lamp.y - 1.5f,
                                                     on ? colour.darker (0.3f) : P.lampOff.darker (0.4f), lamp.x + 2.0f, lamp.y + 2.5f, true));
            g.fillEllipse (lamp.x - 3.5f, lamp.y - 3.5f, 7.0f, 7.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (lamp.x - 3.5f, lamp.y - 3.5f, 7.0f, 7.0f, 0.8f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        if (b.getClickingTogglesState()) r = r.withTrimmedLeft (18.0f);
        g.setColour (palette->ink.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawText (b.getButtonText(), r, juce::Justification::centred, true);
    }

    // Desplegable como ventana empotrada con texto ámbar.
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto& P = *palette;
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        g.setColour (P.inset);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.drawRoundedRectangle (r, 3.0f, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (r.getX() + 3.0f, r.getBottom() - 0.5f, r.getRight() - 3.0f, r.getBottom() - 0.5f, 1.0f);

        juce::Path arrow;
        const float ax = r.getRight() - 12.0f, ay = r.getCentreY();
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

private:
    const Palette* palette = nullptr;
};

// Paleta del tema que usa un componente (según el estilo con el que se dibuja).
inline const Palette& paletteOf (const juce::Component& c)
{
    if (auto* l = dynamic_cast<const EQLookAndFeel*> (&c.getLookAndFeel()))
        return l->getPalette();
    return Themes::get (0);
}
