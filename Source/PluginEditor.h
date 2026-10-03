#pragma once
#include "PluginProcessor.h"
#include "Presets.h"
#include "LookAndFeel.h"
#include "Widgets.h"

// Curva de respuesta + analizador de espectro.
//  - Arrastrar un punto: frecuencia y ganancia (con Mayús, ajuste fino). Rueda sobre un punto: Q. Doble clic: activa/desactiva la banda.
//  - Las asas laterales de la banda enfocada (campanas y notch) cambian su ancho (Q).
//  - En las bandas dinámicas, la zona sombreada muestra cuánta ganancia se está aplicando.
class ResponseCurve : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ResponseCurve (MedidoresEQAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setFocusBand (int band) { focusBand = band; }   // la banda cuyas asas de Q se muestran

private:
    static constexpr int numPoints = 560;   // puntos con los que se dibuja el espectro

    void timerCallback() override;
    void setupFft (int order);
    void updateSpectrum();

    float rangeDb() const;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float d) const;
    float dbForY (float y) const;
    juce::Point<float> nodePos (int band) const;
    int nodeAt (juce::Point<float> p) const;
    bool isBell (int band) const;
    bool isNotch (int band) const;
    float effectiveQ (int band) const;
    juce::Point<float> handlePos (int band, int side) const;   // side: -1 izquierda, +1 derecha
    int handleAt (juce::Point<float> p) const;                 // 0 = ninguna
    void setParam (const juce::String& id, float realValue);
    juce::RangedAudioParameter* param (const juce::String& id) const { return proc.apvts.getParameter (id); }
    void gesture (int band, bool begin);
    void qGesture (int band, bool begin);
    void rebaseDrag (juce::Point<float> p);

    MedidoresEQAudioProcessor& proc;

    // Analizador
    int fftOrder = 0, fftSize = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
    std::vector<float> ring, fftData, bins, prefix;
    std::array<float, numPoints> shown {}, held {}, shownFreq {};
    bool holdWasOn = false;

    int hovered = -1, dragged = -1, focusBand = -1, dragHandle = 0;
    juce::Point<float> dragOrigin;   // para el ajuste fino con Mayús
    float dragStartFreq = 0.0f, dragStartGain = 0.0f;
    bool dragFine = false;
};

// Barra de una banda dinámica: cuánto de su ganancia máxima se está aplicando ahora mismo.
class DynMeter : public juce::Component, private juce::Timer
{
public:
    DynMeter (MedidoresEQAudioProcessor& p, int bandIndex) : proc (p), band (bandIndex) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    MedidoresEQAudioProcessor& proc;
    int band;
};

class MedidoresEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MedidoresEQAudioProcessorEditor (MedidoresEQAudioProcessor&);
    ~MedidoresEQAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    ResponseCurve& getCurve() { return curve; }
    void setDynamicsOpen (bool open);   // despliega o recoge los ajustes de dinámica (cambia el tamaño de la ventana)
    static int windowHeight (bool dynamicsOpen);
    static constexpr int windowWidth = 1280;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // capIndex: banda cuyo color lleva el capuchón; kAccent = color de resalte del tema; kWarm = tono cálido de la saturación.
    static constexpr int kUnused = -99, kAccent = -1, kWarm = -2;
    struct Knob
    {
        FineSlider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
        int capIndex = kUnused;
    };

    void addKnob (Knob& k, const juce::String& paramId, const juce::String& text, int textBoxWidth, int capIndex, const juce::String& tip);
    void addGainKnob (Knob& k, int band, const juce::String& tip);
    void addCombo (juce::ComboBox& box, std::unique_ptr<ComboAttachment>& att, const juce::String& paramId, const juce::StringArray& items);
    void styleKnob (Knob& k);
    void applyBandColours();
    void refreshTypeUi (int band, int type);
    void refreshPresets (const juce::String& select = {});
    void presetChosen();
    void askPresetName();
    void askDeletePreset();
    void timerCallback() override;

    EQLookAndFeel laf;   // el primero: se destruye el último
    MedidoresEQAudioProcessor& proc;
    PresetManager presets;
    juce::TooltipWindow tooltipWindow { this, 500 };

    // Barra superior: presets, ajustes A/B/C/D, deshacer y ajustes del analizador
    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Guardar" }, deleteButton { "Borrar" };
    juce::StringArray factoryNames, userNames;   // los ids del desplegable se reparten entre ambas listas
    juce::TextButton slotButton[4];
    juce::TextButton undoButton { "Deshacer" }, redoButton { "Rehacer" };
    juce::ComboBox analyzerBox, speedBox, resBox, smoothBox, holdBox, rangeBox;
    std::unique_ptr<ComboAttachment> analyzerAttachment, speedAttachment, resAttachment, smoothAttachment, holdAttachment, rangeAttachment;

    ResponseCurve curve;
    VUPair inVU, outVU;
    MeterPanel meterPanel;
    juce::ToggleButton toggles[EQ::NumBands];
    std::unique_ptr<ButtonAttachment> toggleAttachments[EQ::NumBands];
    Knob knobs[EQ::NumBands][3];   // [banda][0=frecuencia, 1=ganancia, 2=Q (o atenuación en el Pultec)]
    std::unique_ptr<RangedSliderAttachment> gainAttachments[EQ::NumBands];
    std::unique_ptr<juce::ParameterAttachment> gainRangeAttachment;
    std::unique_ptr<SegmentedButtons> slopeButtons[EQ::NumBands];       // pendiente (solo filtros de corte)
    std::unique_ptr<SegmentedButtons> placementButtons[EQ::NumBands];   // ST | M | S | L | R (solo campanas y shelves)
    std::unique_ptr<CycleButton> typeButton[EQ::NumBands];              // tipo de la banda
    juce::TextButton soloButton[EQ::NumBands];
    std::unique_ptr<juce::ParameterAttachment> soloAttachment;
    int soloIndex = 0;

    Knob inKnob, outKnob, driveKnob, mixKnob, monoKnob, widthKnob;
    std::unique_ptr<RotarySwitch> characterSwitch, styleSwitch, phaseSwitch, ditherSwitch;
    std::unique_ptr<CycleButton> osButton, gainRangeButton, scButton, detButton;
    juce::ToggleButton bypassToggle, autoGainToggle;
    std::unique_ptr<ButtonAttachment> bypassAttachment, autoGainAttachment;
    juce::TextButton dynExpandButton;
    bool dynOpen = false;

    // EQ dinámico: palanca por banda, con su medidor, modo (compresión/expansión), umbral, ratio, ataque y release.
    juce::ToggleButton dynToggle[EQ::NumBands];
    std::unique_ptr<DynMeter> dynMeter[EQ::NumBands];
    std::unique_ptr<ButtonAttachment> dynAttachments[EQ::NumBands];
    std::unique_ptr<CycleButton> dmodeButton[EQ::NumBands];
    Knob thrKnob[EQ::NumBands], ratioKnob[EQ::NumBands], attackKnob[EQ::NumBands], releaseKnob[EQ::NumBands];

    // Geometría de la placa (se calcula en resized y se dibuja en paint)
    struct Section { juce::Rectangle<int> bounds; juce::String title; };
    std::vector<Section> sections;
    juce::Rectangle<int> bezelRect, stripRect;
    int filterSplitY = 0, sectionTitleY = 0;
    juce::Image plate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MedidoresEQAudioProcessorEditor)
};
