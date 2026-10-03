#pragma once
#include "PluginProcessor.h"
#include "LookAndFeel.h"

// Controles de hardware que sustituyen a los desplegables y los medidores de barra.

// Pulsadores de posiciones fijas en una fila (p. ej. Estéreo | Mid | Side, o 6 | 12 | 24 | 48 dB/oct): el activo se hunde y enciende su piloto.
class SegmentedButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedButtons (juce::RangedAudioParameter& param, const juce::StringArray& labels, juce::Colour lampColour);
    void setLampColour (juce::Colour c) { lamp = c; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void select (int index);
    juce::StringArray labels;
    juce::Colour lamp;
    int current = 0;
    juce::ParameterAttachment attachment;
};

// Selector rotativo de posiciones: un knob con las posiciones impresas alrededor, como el de un equipo de rack.
class RotarySwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    RotarySwitch (juce::RangedAudioParameter& param, const juce::String& title, const juce::StringArray& labels, juce::Colour capColour);
    void setCapColour (juce::Colour c) { cap = c; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { setFromPoint (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { setFromPoint (e.position); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr float startAngle = -2.356f, endAngle = 2.356f;   // -135º .. +135º desde arriba, en sentido horario
    struct Geometry { juce::Point<float> c; float r, lx, ly; };
    Geometry geometry() const;
    float angleFor (int index) const;
    void setFromPoint (juce::Point<float> p);
    void select (int index);

    juce::String title;
    juce::StringArray labels;
    juce::Colour cap;
    int current = 0;
    juce::ParameterAttachment attachment;
};

// Dos medidores VU de aguja (L y R, apilados) para la entrada o la salida, con balística, piloto de pico y pico máximo en cifras.
class VUPair : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    VUPair (MedidoresEQAudioProcessor& p, bool isInput);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { held = -100.0f; }   // clic: borra el pico máximo

private:
    void timerCallback() override;
    void drawFace (juce::Graphics&, juce::Rectangle<float> face, const juce::String& tag, float level, bool led);

    MedidoresEQAudioProcessor& proc;
    bool input;
    float level[2] { 0.0f, 0.0f };
    int ledFrames[2] { 0, 0 };
    float held = -100.0f;
};

// Slider que admite ajuste fino: con Mayús pulsada al empezar a arrastrar el knob se mueve unas seis veces más despacio.
class FineSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;
    void mouseDown (const juce::MouseEvent& e) override
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? 1600 : 250);
        juce::Slider::mouseDown (e);
    }
};

// Une un slider a un parámetro pero con un rango propio (±limit): sirve para los knobs de ganancia con rango conmutable (±18/±12/±6/±3).
class RangedSliderAttachment
{
public:
    RangedSliderAttachment (juce::RangedAudioParameter& param, juce::Slider& slider);
    ~RangedSliderAttachment();
    void setLimit (double limit);   // cambia el rango visible del knob

private:
    juce::RangedAudioParameter& param;
    juce::Slider& slider;
    bool updating = false, dragging = false;
    double limit = 18.0;
    juce::ParameterAttachment attachment;
};

// Botón que recorre las posiciones de un parámetro de elección cada vez que se pulsa (el texto muestra la posición actual).
class CycleButton : public juce::TextButton
{
public:
    CycleButton (juce::RangedAudioParameter& param, const juce::StringArray& labels, const juce::String& prefix = {});
    std::function<void (int)> onChanged;   // llamado al cambiar la posición (también por automatización)
    void refresh() { attachment.sendInitialUpdate(); }
    int getIndex() const { return current; }

private:
    juce::StringArray labels;
    juce::String prefix;
    int current = 0;
    juce::ParameterAttachment attachment;
};

// Medición de la salida: goniómetro (vectorscopio), correlación estéreo y loudness (M / S / I) con true peak.
// Clic: borra el integrado y el pico.
class MeterPanel : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit MeterPanel (MedidoresEQAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.resetLoudness(); }

private:
    void timerCallback() override;
    MedidoresEQAudioProcessor& proc;
    static constexpr int maxPoints = 700;
    std::vector<float> points;   // pares (x, y) ya rotados, de más antiguo a más reciente
    float correlation = 1.0f, smoothCorr = 1.0f;
};

// Chapa frontal con grano de cepillado y viñeteado (se genera una vez por tamaño y tema).
juce::Image makePlate (int width, int height, const Palette& palette);

// Tornillo de cabeza ranurada.
void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float slotAngle);
