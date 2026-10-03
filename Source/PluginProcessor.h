#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include "EQ.h"
#include "Meters.h"

// eCU-10 MST: ecualizador de mastering. Todo el procesado interno es en doble precisión.
//
//  Cadena: ganancia de entrada -> EQ (fase mínima o fase lineal) -> anchura / graves en mono -> saturación (2x) ->
//          ganancia de salida -> compensación de nivel -> bypass (con la misma latencia) -> dither.
//
//  Latencia: la del sobremuestreo de la saturación (siempre, para que no cambie con la mezcla en paralelo) más, en fase lineal,
//  la mitad del filtro FIR. Se informa al host y el bypass se retrasa lo mismo, así la comparación A/B está alineada en el tiempo.
class MedidoresEQAudioProcessor : public juce::AudioProcessor,
                                  private juce::AsyncUpdater,
                                  private juce::Timer,
                                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    MedidoresEQAudioProcessor();
    ~MedidoresEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "eCU-10 MST"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undoManager;   // antes que apvts: se pasa al constructor de este
    juce::AudioProcessorValueTreeState apvts;

    // Muestras (mono) para el analizador de espectro del editor.
    int pullAnalyzerSamples (float* dest, int maxSamples);

    // Pico de cada canal desde la última lectura (lineal). Lo lee el editor para los medidores.
    float takeInputPeak (int channel)  { return inPeak[channel & 1].exchange (0.0f); }
    float takeOutputPeak (int channel) { return outPeak[channel & 1].exchange (0.0f); }
    // RMS de cada canal (lineal, integración de ~300 ms).
    float getInputRms (int channel) const  { return inRms[channel & 1].load(); }
    float getOutputRms (int channel) const { return outRms[channel & 1].load(); }

    // Ganancia que está aplicando ahora mismo una banda dinámica (dB; 0 si no es dinámica). La lee el editor.
    float getDynamicGainDb (int band) const { return dynGainDb[band].load(); }

    // Medición de la salida: loudness (LUFS), true peak, correlación estéreo y puntos del goniómetro.
    struct Loudness { float momentary, shortTerm, integrated, truePeakDb; };
    Loudness getLoudness() const;
    void resetLoudness();
    float getCorrelation() const { return correlation.load(); }
    int pullGoniometer (float* dest, int maxPairs);   // pares (L, R) intercalados
    float getAutoGainDb() const { return autoGainDb.load(); }

    // Cuatro ajustes (A/B/C/D) del plugin: al cambiar de ranura se guarda la actual y se carga la otra.
    int getActiveSlot() const { return activeSlot; }
    void switchSlot (int slot);

    // Longitud (muestras) del filtro FIR según la calidad (0 baja, 1 media, 2 alta) y la frecuencia de muestreo.
    static int firLengthFor (int quality, double sampleRate);

    // Latencia total (muestras) con el modo de fase y el sobremuestreo actuales. Se aplica sola al cambiar el modo de fase.
    int computeLatency() const;
    void refreshLatency();
    // Reconstruye los filtros FIR del modo de fase lineal (se llama sola desde un temporizador al mover los controles).
    void rebuildKernels();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    //--------------------------------------------------------------------------
    struct Params;   // lectura de los parámetros globales de un bloque
    void updateTargets (bool force);
    void processChunk (int n, const Params&);
    void processBand (int band, int n);
    void updateDynamic (int band, int where, int start, int len);
    void applyWidthAndMono (int n, const Params&);
    void applyConfigSwitch();
    void saturate (int n, const Params&);
    void runFir (int n);
    void runSolo (int band, int n);
    void pushAnalyzerSamples (const double* l, const double* r, int n);
    void updateRms (double* ms, std::atomic<float>* out, const double* l, const double* r, int n);

    double* ch (int c) { return work[(size_t) c].data(); }

    double currentRate = 44100.0;
    int maxBlockSize = 512;

    // Señales de trabajo (doble precisión, dos canales siempre: en mono el segundo es copia del primero).
    std::array<std::vector<double>, 2> work, raw, scBuf, scratch, dryBuf;
    bool scAvailable = false, useExtSc = false, detRms = false;

    //--------------------------------------------------------------------------
    // Bandas
    struct BandState
    {
        EQ::Design target;                 // coeficientes que se quieren alcanzar
        EQ::Bq cur[EQ::MaxStages];         // coeficientes actuales (se acercan a los objetivo para evitar clics al automatizar)
        double z1[EQ::MaxStages][2] {}, z2[EQ::MaxStages][2] {};   // estados (TDF-II) [etapa][canal]
        int used = 1;
        int where = 0;                     // 0 estéreo, 1 Mid, 2 Side, 3 izquierdo, 4 derecho
        bool on = false;

        // EQ dinámico
        bool dyn = false, expand = false;
        EQ::Settings settings;
        float threshold = -24.0f, ratio = 3.0f, attackCoef = 0.0f, releaseCoef = 0.0f, envelope = 0.0f;
        EQ::Bq detector;
        double dz1 = 0.0, dz2 = 0.0;
    };
    BandState band[EQ::NumBands];
    std::array<float, 12> lastParams[EQ::NumBands] {};
    double smoothCoef = 0.2;
    static constexpr int segment = 32;   // cada cuántas muestras se actualizan los coeficientes (suavizado y dinámica)

    //--------------------------------------------------------------------------
    // Fase lineal: dos convoluciones (matriz 2x2 de filtros: L->L, R->R y los cruces) con filtros de fase cero centrados.
    juce::dsp::Convolution convDirect { juce::dsp::Convolution::Latency { 0 } };
    juce::dsp::Convolution convCross  { juce::dsp::Convolution::Latency { 0 } };
    juce::AudioBuffer<float> firBuf, firBufCross;
    int firLength = 16384, firQuality = 1, kernelFftSize = 0;
    std::atomic<bool> rebuildRequested { false };
    std::atomic<bool> kernelsReady { false };
    std::array<float, 160> lastKernelKey {};
    int phaseMode = 0;   // 0 mínima, 1 natural, 2 lineal (el que está activo en el hilo de audio)
    std::unique_ptr<juce::dsp::FFT> kernelFft;

    //--------------------------------------------------------------------------
    // Graves en mono y anchura
    struct MonoFilter { EQ::Bq c[2]; double z1[2] {}, z2[2] {}; } monoFilter;
    float lastMonoFreq = -1.0f;
    EQ::Bq soloCoef;
    double soloZ1[2] {}, soloZ2[2] {};

    // Saturación analógica (cinta/válvula) con sobremuestreo 2x.
    // Filtros FIR de fase lineal: retrasan todas las frecuencias exactamente igual, así la señal seca (retardada con la misma latencia)
    // y la saturada quedan alineadas en todo el espectro y la mezcla en paralelo no hace peine. Latencia entera.
    juce::dsp::Oversampling<float> os2 { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false, true };
    juce::dsp::Oversampling<float> os4 { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false, true };
    juce::AudioBuffer<float> satBuf;
    int osLatency = 0, osLatency2 = 0, osLatency4 = 0, osActive = 0;   // osActive: 0 = 2x, 1 = 4x
    // Cambio de configuración (fase, calidad, sobremuestreo): se silencia con un fundido corto, se cambia y se vuelve a subir.
    int wantPhase = 0, wantOs = 0, wantQuality = 1;
    bool ducking = false;
    juce::SmoothedValue<double> duckSm, deltaSm;
    double inDcX[2] {}, inDcY[2] {};
    DelayLine dryDelay[2], bypassDelay[2], firPass[2];   // firPass: retardo equivalente al FIR mientras este no está listo
    float lastAmount = 0.0f;
    bool satWasActive = false;
    double dcX[2] {}, dcY[2] {};

    // Ganancias suavizadas
    juce::SmoothedValue<double> inGainSm, outGainSm, bypassSm, autoSm;

    // Medición
    LoudnessMeter inLoud, procLoud, outLoud;
    TruePeakMeter truePeak;
    std::atomic<float> inPeak[2] { 0.0f, 0.0f }, outPeak[2] { 0.0f, 0.0f };
    std::atomic<float> inRms[2] { 0.0f, 0.0f }, outRms[2] { 0.0f, 0.0f };
    double inMs[2] {}, outMs[2] {};   // potencia media suavizada
    std::atomic<float> dynGainDb[EQ::NumBands] {};
    std::atomic<float> correlation { 1.0f }, autoGainDb { 0.0f };
    double corrLR = 0.0, corrLL = 0.0, corrRR = 0.0;
    double autoGainState = 0.0;
    int gonioCounter = 0;

    juce::AbstractFifo analyzerFifo { 65536 };
    std::vector<float> analyzerData = std::vector<float> (65536, 0.0f);
    juce::AbstractFifo gonioFifo { 4096 };
    std::vector<float> gonioData = std::vector<float> (4096 * 2, 0.0f);

    // Dither
    juce::uint32 rngState = 22222u;
    inline double randomUnit() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (double) rngState / 4294967296.0; }

    // Ajustes A/B/C/D
    juce::String slotXml[4];
    int activeSlot = 0;

    juce::RangedAudioParameter* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MedidoresEQAudioProcessor)
};
