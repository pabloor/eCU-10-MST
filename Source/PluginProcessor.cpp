#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <complex>

namespace
{
    using Cx = std::complex<float>;

    // Clave con todo lo que cambia los filtros: si no cambia, no hace falta recalcular el FIR.
    std::array<float, 160> kernelKey (const juce::AudioProcessorValueTreeState& apvts, double sampleRate)
    {
        std::array<float, 160> key {};
        auto read = [&] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
        key[0] = read (EQ::phaseId);
        key[1] = (float) sampleRate;
        key[2] = read (EQ::styleId);
        int i = 3;
        for (int b = 0; b < EQ::NumBands; ++b)
        {
            key[(size_t) i++] = read (EQ::onId (b));
            key[(size_t) i++] = read (EQ::freqId (b));
            if (EQ::isCut (b))
            {
                key[(size_t) i++] = read (EQ::slopeId (b));
                i += 7;
                continue;
            }
            key[(size_t) i++] = read (EQ::gainId (b));
            key[(size_t) i++] = read (EQ::qId (b));
            key[(size_t) i++] = read (EQ::typeId (b));
            key[(size_t) i++] = EQ::isShelf (b) ? read (EQ::cutId (b)) : 0.0f;
            key[(size_t) i++] = read (EQ::dynId (b));
            key[(size_t) i++] = read (EQ::chId (b));
            i += 1;
        }
        return key;
    }
}

struct MedidoresEQAudioProcessor::Params
{
    int character = 0, phase = 0, dither = 0, solo = 0, analyzer = 1, osChoice = 0;
    float drive = 0.0f, mix = 100.0f, monoFreq = 0.0f, width = 100.0f, inDb = 0.0f, outDb = 0.0f;
    bool bypass = false, autoGain = false, extSc = false, rms = false;
};

//==============================================================================
MedidoresEQAudioProcessor::MedidoresEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", createLayout())
{
    bypassParam = apvts.getParameter (EQ::bypassId);

    // El host puede consultar la latencia antes de preparar la reproducción: se fija ya aquí.
    os2.initProcessing (512);
    os4.initProcessing (512);
    osLatency2 = juce::roundToInt (os2.getLatencyInSamples());
    osLatency4 = juce::roundToInt (os4.getLatencyInSamples());
    osLatency = osLatency2;
    setLatencySamples (computeLatency());

    apvts.addParameterListener (EQ::phaseId, this);
    apvts.addParameterListener (EQ::osId, this);
    startTimerHz (20);
}

MedidoresEQAudioProcessor::~MedidoresEQAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    apvts.removeParameterListener (EQ::phaseId, this);
    apvts.removeParameterListener (EQ::osId, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout MedidoresEQAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Textos con pocos decimales: Hz sin decimales (kHz con 1-2), dB con 1, Q con 1.
    auto hzText = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto hzParse = [] (const String& t) { float v = t.getFloatValue(); return t.containsIgnoreCase ("k") ? v * 1000.0f : v; };
    auto dbText = [] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; };
    auto numParse = [] (const String& t) { return t.getFloatValue(); };
    auto qText = [] (float v, int) { return String (v, 1); };
    auto pctText = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto ratioText = [] (float v, int) { return String (v, 1) + ":1"; };
    auto msText = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto thrText = [] (float v, int) { return String (roundToInt (v)) + " dB"; };
    auto cutText = [] (float v, int) { return "-" + String (v, 1) + " dB"; };
    auto monoText = [] (float v, int) { return v < 1.0f ? String ("Off") : String (roundToInt (v)) + " Hz"; };

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const auto& info = EQ::bands[b];
        const String name (info.name);

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { EQ::onId (b), 1 }, name + " on", true));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::freqId (b), 1 }, name + " frequency",
            NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), info.freq,
            AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText).withValueFromStringFunction (hzParse)));

        if (EQ::isCut (b))
        {
            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { EQ::slopeId (b), 1 }, name + " slope", EQ::slopeNames(), 1));
            continue;
        }

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::gainId (b), 1 }, name + " gain",
            NormalisableRange<float> (-18.0f, 18.0f, 0.1f), info.gain,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withValueFromStringFunction (numParse)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::qId (b), 1 }, name + " Q",
            NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f), info.q,
            AudioParameterFloatAttributes().withStringFromValueFunction (qText).withValueFromStringFunction (numParse)));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { EQ::typeId (b), 1 }, name + " type", EQ::typeNames (b), 0));

        if (EQ::isShelf (b))   // atenuación del modo Pultec
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { EQ::cutId (b), 1 }, name + " cut",
                NormalisableRange<float> (0.0f, 18.0f, 0.1f), 6.0f,
                AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (cutText).withValueFromStringFunction (numParse)));

        // EQ dinámico: la ganancia (hasta el valor del knob de ganancia) solo se aplica cuando el nivel
        // en la banda supera el umbral (compresión) o queda por debajo de él (expansión).
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { EQ::dynId (b), 1 }, name + " dynamic", false));
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { EQ::dmodeId (b), 1 }, name + " dynamic mode", EQ::dmodeNames(), 0));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::thrId (b), 1 }, name + " threshold",
            NormalisableRange<float> (-60.0f, 0.0f, 1.0f), -24.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (thrText).withValueFromStringFunction (numParse)));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::ratioId (b), 1 }, name + " ratio",
            NormalisableRange<float> (1.2f, 10.0f, 0.1f, 0.5f), 3.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (ratioText).withValueFromStringFunction (numParse)));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::attackId (b), 1 }, name + " attack",
            NormalisableRange<float> (0.5f, 100.0f, 0.5f, 0.5f), 10.0f,
            AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText).withValueFromStringFunction (numParse)));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::releaseId (b), 1 }, name + " release",
            NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.4f), 150.0f,
            AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText).withValueFromStringFunction (numParse)));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { EQ::chId (b), 1 }, name + " channel", EQ::placementNames(), 0));
    }

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::styleId, 1 }, "Curve style", EQ::styleNames(), 1));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::phaseId, 1 }, "Phase", EQ::phaseNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::scId, 1 }, "Dynamics detector", EQ::scNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::detId, 1 }, "Detector type", EQ::detNames(), 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::monoFreqId, 1 }, "Bass mono",
        NormalisableRange<float> (0.0f, 300.0f, 1.0f, 0.6f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (monoText).withValueFromStringFunction (numParse)));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::widthId, 1 }, "Width",
        NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pctText).withValueFromStringFunction (numParse)));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::ditherId, 1 }, "Dither", EQ::ditherNames(), 0));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { EQ::bypassId, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { EQ::autoGainId, 1 }, "Match loudness", false,
                                                     AudioParameterBoolAttributes().withAutomatable (false)));

    // Ajustes de la vista (no se automatizan).
    auto view = AudioParameterChoiceAttributes().withAutomatable (false);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { EQ::analyzerId, 1 }, "Analyzer", EQ::analyzerNames(), 1, view));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { EQ::analyzerSpeedId, 1 }, "Analyzer speed", EQ::speedNames(), 1, view));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { EQ::analyzerResId, 1 }, "Analyzer resolution", EQ::analyzerResNames(), 1, view));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { EQ::analyzerSmoothId, 1 }, "Analyzer smoothing", EQ::analyzerSmoothNames(), 0, view));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { EQ::analyzerHoldId, 1 }, "Analyzer peak hold", EQ::holdNames(), 0, view));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { EQ::rangeId, 1 }, "Curve range", EQ::rangeNames(), 1, view));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { EQ::gainRangeId, 1 }, "Gain range", EQ::gainRangeNames(), 0, view));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { EQ::soloId, 1 }, "Band solo", EQ::soloNames(), 0, view));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::characterId, 1 }, "Character", EQ::characterNames(), 1));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::osId, 1 }, "Oversampling", EQ::osNames(), 0));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::driveId, 1 }, "Drive",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pctText).withValueFromStringFunction (numParse)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::mixId, 1 }, "Saturation mix",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pctText).withValueFromStringFunction (numParse)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::inId, 1 }, "Input",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withValueFromStringFunction (numParse)));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::outId, 1 }, "Output",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withValueFromStringFunction (numParse)));

    return layout;
}

bool MedidoresEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (! (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())) return false;
    if (out != layouts.getMainInputChannelSet()) return false;

    if (layouts.inputBuses.size() > 1)   // sidechain opcional, mono o estéreo
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

//==============================================================================
int MedidoresEQAudioProcessor::computeLatency() const
{
    const int os = (int) apvts.getRawParameterValue (EQ::osId)->load() == 1 ? osLatency4 : osLatency2;
    const int phase = (int) apvts.getRawParameterValue (EQ::phaseId)->load();
    return os + (phase > 0 ? firLength / 2 : 0);
}

void MedidoresEQAudioProcessor::refreshLatency()
{
    setLatencySamples (computeLatency());
}

void MedidoresEQAudioProcessor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }

void MedidoresEQAudioProcessor::handleAsyncUpdate()
{
    refreshLatency();
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));
}

void MedidoresEQAudioProcessor::timerCallback()
{
    if ((int) apvts.getRawParameterValue (EQ::phaseId)->load() == 0) return;
    if (kernelKey (apvts, currentRate) != lastKernelKey) rebuildKernels();
}

void MedidoresEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    os2.initProcessing ((size_t) maxBlockSize);
    os4.initProcessing ((size_t) maxBlockSize);
    osLatency2 = juce::roundToInt (os2.getLatencyInSamples());
    osLatency4 = juce::roundToInt (os4.getLatencyInSamples());
    osActive = (int) apvts.getRawParameterValue (EQ::osId)->load() == 1 ? 1 : 0;
    osLatency = osActive == 1 ? osLatency4 : osLatency2;

    // Longitud del filtro FIR del modo de fase lineal: más larga a frecuencias de muestreo altas para mantener la resolución en graves.
    firLength = sampleRate <= 50000.0 ? 16384 : (sampleRate <= 100000.0 ? 32768 : 65536);
    kernelFft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) firLength)));

    for (auto* v : { &work, &raw, &scBuf, &scratch, &dryBuf })
        for (auto& c : *v) c.assign ((size_t) maxBlockSize, 0.0);
    satBuf.setSize (2, maxBlockSize);
    firBuf.setSize (2, maxBlockSize);
    firBufCross.setSize (2, maxBlockSize);

    for (int c = 0; c < 2; ++c)
    {
        dryDelay[c].prepare (juce::jmax (osLatency2, osLatency4) + 2);
        bypassDelay[c].prepare (firLength / 2 + juce::jmax (osLatency2, osLatency4) + 2);
    }
    satWasActive = false;
    lastAmount = 0.0f;
    lastMonoFreq = -1.0f;
    monoFilter = {};
    for (int c = 0; c < 2; ++c) { soloZ1[c] = soloZ2[c] = 0.0; dcX[c] = dcY[c] = 0.0; }

    inLoud.prepare (sampleRate);
    procLoud.prepare (sampleRate);
    outLoud.prepare (sampleRate);
    truePeak.reset();
    corrLR = corrLL = corrRR = 0.0;
    autoGainState = 0.0;
    inMs[0] = inMs[1] = outMs[0] = outMs[1] = 0.0;

    smoothCoef = 1.0 - std::exp (-(double) segment / (0.004 * sampleRate));   // glissando de coeficientes de ~4 ms
    auto prepSmoother = [&] (juce::SmoothedValue<double>& s, double seconds, double value)
    {
        s.reset (sampleRate, seconds);
        s.setCurrentAndTargetValue (value);
    };
    prepSmoother (inGainSm,  0.02, juce::Decibels::decibelsToGain ((double) apvts.getRawParameterValue (EQ::inId)->load()));
    prepSmoother (outGainSm, 0.02, juce::Decibels::decibelsToGain ((double) apvts.getRawParameterValue (EQ::outId)->load()));
    prepSmoother (bypassSm,  0.01, apvts.getRawParameterValue (EQ::bypassId)->load() > 0.5f ? 1.0 : 0.0);
    prepSmoother (autoSm,    0.5,  1.0);

    for (auto& b : band)
    {
        for (auto& st : b.z1) for (auto& z : st) z = 0.0;
        for (auto& st : b.z2) for (auto& z : st) z = 0.0;
        b.dz1 = b.dz2 = 0.0;
        b.envelope = 0.0f;
    }
    updateTargets (true);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };
    convDirect.prepare (spec);
    convCross.prepare (spec);
    kernelsReady = false;
    lastKernelKey.fill (-1.0f);
    phaseMode = (int) apvts.getRawParameterValue (EQ::phaseId)->load();
    if (phaseMode != 0) rebuildKernels();

    refreshLatency();
}

//==============================================================================
// Recalcula los coeficientes objetivo solo de las bandas cuyos parámetros han cambiado.
void MedidoresEQAudioProcessor::updateTargets (bool force)
{
    auto read = [&] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        auto& s = band[b];
        s.where = EQ::placement (b, apvts);

        const std::array<float, 12> now {
            read (EQ::onId (b)), read (EQ::freqId (b)),
            EQ::isCut (b) ? read (EQ::slopeId (b)) : read (EQ::gainId (b)),
            EQ::isCut (b) ? 0.0f : read (EQ::qId (b)), read (EQ::styleId),
            EQ::hasType (b) ? read (EQ::typeId (b)) : 0.0f,
            EQ::isShelf (b) ? read (EQ::cutId (b)) : 0.0f,
            EQ::hasDyn (b) ? read (EQ::dynId (b)) : 0.0f,
            EQ::hasDyn (b) ? read (EQ::dmodeId (b)) : 0.0f, 0.0f, 0.0f, 0.0f };

        if (! force && now == lastParams[b]) continue;
        lastParams[b] = now;

        s.on = now[0] > 0.5f;
        s.target = EQ::makeBand (b, apvts, currentRate);

        if (EQ::hasDyn (b))
        {
            s.settings = EQ::readSettings (b, apvts, currentRate);
            s.dyn = now[7] > 0.5f && s.on && EQ::dynEligible (s.settings.shape);
            s.expand = now[8] > 0.5f;
            // Detector: paso de banda en las campanas, paso bajo en el shelf de graves, paso alto en el de agudos.
            s.detector = s.settings.shape == EQ::Peak ? EQ::rbjBandPass (currentRate, s.settings.freq, juce::jmax (0.5f, s.settings.q))
                       : s.settings.shape == EQ::LowShelfShape ? EQ::rbjLowPass (currentRate, s.settings.freq, 0.707)
                                                               : EQ::rbjHighPass (currentRate, s.settings.freq, 0.707);
        }

        if (force)
        {
            for (int i = 0; i < EQ::MaxStages; ++i) s.cur[i] = i < s.target.n ? s.target.st[i] : EQ::Bq {};
            s.used = s.target.n;
        }
    }

    for (int b = 0; b < EQ::NumBands; ++b)
        if (EQ::hasDyn (b))
        {
            auto& d = band[b];
            d.threshold = read (EQ::thrId (b));
            d.ratio = juce::jmax (1.01f, read (EQ::ratioId (b)));
            d.attackCoef  = std::exp (-1.0f / (juce::jmax (0.1f, read (EQ::attackId (b)))  * 0.001f * (float) currentRate));
            d.releaseCoef = std::exp (-1.0f / (juce::jmax (0.1f, read (EQ::releaseId (b))) * 0.001f * (float) currentRate));
        }
}

//==============================================================================
namespace
{
    // Biquad TDF-II en doble precisión, en el sitio.
    inline void runBiquad (double* x, int n, const EQ::Bq& c, double& z1, double& z2)
    {
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            const double y = c.b0 * in + z1;
            z1 = c.b1 * in - c.a1 * y + z2;
            z2 = c.b2 * in - c.a2 * y;
            x[i] = y;
        }
    }
}

// EQ dinámico: mide el nivel de la señal en la banda y ajusta la ganancia del filtro.
// Compresión: con el nivel por encima del umbral, la ganancia sube (o baja) progresivamente según el ratio, hasta el valor del
// knob de ganancia; por debajo del umbral la banda queda plana. Expansión: al revés (actúa por debajo del umbral).
void MedidoresEQAudioProcessor::updateDynamic (int b, int where, int start, int len)
{
    auto& d = band[b];
    const double* a = ch (0);
    const double* c = ch (1);

    float env = d.envelope;
    for (int i = start; i < start + len; ++i)
    {
        double x;
        if (useExtSc)
        {
            const double l = scBuf[0][(size_t) i], r = scBuf[1][(size_t) i];
            x = where == 0 ? 0.5 * (l + r) : where == 1 ? 0.5 * (l + r) : where == 2 ? 0.5 * (l - r) : where == 3 ? l : r;
        }
        else
        {
            x = where == 0 ? 0.5 * (a[i] + c[i]) : (where == 1 || where == 3) ? a[i] : c[i];
        }

        // detector: filtro de la banda + valor absoluto (pico) o cuadrado (RMS)
        const double y = d.detector.b0 * x + d.dz1;
        d.dz1 = d.detector.b1 * x - d.detector.a1 * y + d.dz2;
        d.dz2 = d.detector.b2 * x - d.detector.a2 * y;
        const float r = detRms ? (float) (y * y) : (float) std::abs (y);
        env = r > env ? r + d.attackCoef * (env - r) : r + d.releaseCoef * (env - r);
    }
    d.envelope = env;

    const float level = detRms ? std::sqrt (env) : env;
    const float levelDb = juce::Decibels::gainToDecibels (level, -100.0f);
    const float over = d.expand ? d.threshold - levelDb : levelDb - d.threshold;
    const float reduction = over > 0.0f ? juce::jmin (over * (1.0f - 1.0f / d.ratio), std::abs (d.settings.gainDb)) : 0.0f;
    const float gainDb = d.settings.gainDb >= 0.0f ? reduction : -reduction;
    dynGainDb[b].store (gainDb);

    d.target = EQ::makeDesign (d.settings, gainDb, currentRate);
}

void MedidoresEQAudioProcessor::processBand (int b, int n)
{
    auto& s = band[b];
    if (EQ::hasDyn (b) && ! s.dyn) dynGainDb[b].store (0.0f);
    if (! s.on && ! s.dyn && s.used == 0) return;   // banda apagada y ya sin efecto

    const int where = s.where;
    double* l = ch (0);
    double* r = ch (1);

    if (where == 1 || where == 2)   // Mid/Side: canal 0 = Mid, canal 1 = Side
        for (int i = 0; i < n; ++i)
        {
            const double m = 0.5 * (l[i] + r[i]), sd = 0.5 * (l[i] - r[i]);
            l[i] = m; r[i] = sd;
        }

    const double k = s.dyn ? 1.0 : smoothCoef;
    for (int start = 0; start < n; start += segment)
    {
        const int len = juce::jmin (segment, n - start);
        if (s.dyn) updateDynamic (b, where, start, len);

        // Los coeficientes se acercan a los objetivo (evita clics al automatizar).
        int used = 0;
        for (int i = 0; i < EQ::MaxStages; ++i)
        {
            const EQ::Bq tgt = i < s.target.n ? s.target.st[i] : EQ::Bq {};
            auto& c = s.cur[i];
            c.b0 += (tgt.b0 - c.b0) * k; c.b1 += (tgt.b1 - c.b1) * k; c.b2 += (tgt.b2 - c.b2) * k;
            c.a1 += (tgt.a1 - c.a1) * k; c.a2 += (tgt.a2 - c.a2) * k;
            if (c.isNeutral())
            {
                if (tgt.isNeutral()) c = EQ::Bq {};
            }
            else used = i + 1;
        }
        s.used = used;

        for (int i = 0; i < used; ++i)
        {
            const auto& c = s.cur[i];
            if (where == 0)
            {
                runBiquad (l + start, len, c, s.z1[i][0], s.z2[i][0]);
                runBiquad (r + start, len, c, s.z1[i][1], s.z2[i][1]);
            }
            else
            {
                const int chan = where <= 2 ? where - 1 : where - 3;   // Mid/Side: 0/1; izquierdo/derecho: 0/1
                runBiquad (ch (chan) + start, len, c, s.z1[i][chan], s.z2[i][chan]);
            }
        }
    }

    if (where == 1 || where == 2)
        for (int i = 0; i < n; ++i)
        {
            const double m = l[i], sd = r[i];
            l[i] = m + sd; r[i] = m - sd;
        }
}

//==============================================================================
// Fase lineal / natural: matriz 2x2 de filtros (L->L, R->R y los cruces que aparecen con las bandas Mid/Side)
// aplicada con dos convoluciones. Los filtros FIR están centrados en N/2: de ahí su latencia.
void MedidoresEQAudioProcessor::runFir (int n)
{
    if (! kernelsReady.load()) return;

    double* l = ch (0);
    double* r = ch (1);
    float* d0 = firBuf.getWritePointer (0);
    float* d1 = firBuf.getWritePointer (1);
    float* c0 = firBufCross.getWritePointer (0);
    float* c1 = firBufCross.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        d0[i] = (float) l[i]; d1[i] = (float) r[i];
        c0[i] = (float) r[i]; c1[i] = (float) l[i];   // el canal cruzado lleva los canales intercambiados
    }

    {
        juce::dsp::AudioBlock<float> block (firBuf);
        auto sub = block.getSubBlock (0, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        convDirect.process (ctx);
    }
    {
        juce::dsp::AudioBlock<float> block (firBufCross);
        auto sub = block.getSubBlock (0, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        convCross.process (ctx);
    }

    for (int i = 0; i < n; ++i)
    {
        l[i] = (double) d0[i] + (double) c0[i];
        r[i] = (double) d1[i] + (double) c1[i];
    }
}

void MedidoresEQAudioProcessor::rebuildKernels()
{
    const int mode = (int) apvts.getRawParameterValue (EQ::phaseId)->load();
    if (mode == 0 || kernelFft == nullptr) return;

    lastKernelKey = kernelKey (apvts, currentRate);
    const int N = firLength, half = N / 2;
    const double sr = currentRate;

    // Respuesta en magnitud de cada cadena: 0 = izquierdo, 1 = derecho, 2 = Mid, 3 = Side (el estéreo cuenta en las dos primeras y
    // también, como es lineal, en Mid y Side: se resuelve al combinarlas). Las bandas dinámicas no entran: van en filtros IIR.
    std::vector<double> mag[4];
    for (auto& m : mag) m.assign ((size_t) half + 1, 1.0);

    std::vector<double> bandMag ((size_t) half + 1);
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        if (apvts.getRawParameterValue (EQ::onId (b))->load() < 0.5f) continue;
        if (band[b].dyn) continue;

        const auto design = EQ::makeBand (b, apvts, sr);
        const int where = EQ::placement (b, apvts);
        for (int k = 0; k <= half; ++k) bandMag[(size_t) k] = design.magnitude ((double) k * sr / (double) N, sr);

        auto apply = [&] (int target) { for (int k = 0; k <= half; ++k) mag[target][(size_t) k] *= bandMag[(size_t) k]; };
        if (where == 0)      { apply (0); apply (1); }
        else if (where == 1) apply (2);
        else if (where == 2) apply (3);
        else if (where == 3) apply (0);
        else                 apply (1);
    }

    // Fase: cero en el modo lineal; la mitad de la fase mínima en el natural (menos pre-eco).
    std::vector<Cx> a ((size_t) N), c ((size_t) N);
    auto phaseOf = [&] (const std::vector<double>& m)
    {
        std::vector<double> phase ((size_t) half + 1, 0.0);
        if (mode != 1) return phase;

        for (int k = 0; k <= half; ++k)
        {
            const float v = (float) std::log (juce::jmax (m[(size_t) k], 1.0e-6));
            a[(size_t) k] = v;
            if (k > 0 && k < half) a[(size_t) (N - k)] = v;
        }
        kernelFft->perform (a.data(), c.data(), true);   // cepstro real
        for (int n = 0; n < N; ++n)
        {
            const float v = c[(size_t) n].real();
            a[(size_t) n] = n == 0 || n == half ? v : (n < half ? 2.0f * v : 0.0f);   // cepstro de fase mínima
        }
        kernelFft->perform (a.data(), c.data(), false);
        for (int k = 0; k <= half; ++k) phase[(size_t) k] = 0.5 * (double) c[(size_t) k].imag();
        return phase;
    };

    std::vector<std::complex<double>> z[4];
    for (int i = 0; i < 4; ++i)
    {
        const auto ph = phaseOf (mag[i]);
        z[i].resize ((size_t) half + 1);
        for (int k = 0; k <= half; ++k) z[i][(size_t) k] = std::polar (mag[i][(size_t) k], ph[(size_t) k]);
    }

    // y = Dec · diag(Hm, Hs) · Enc · diag(Hl, Hr) · x
    std::vector<std::complex<double>> hLL ((size_t) half + 1), hRR ((size_t) half + 1), hLR ((size_t) half + 1), hRL ((size_t) half + 1);
    for (int k = 0; k <= half; ++k)
    {
        const auto zl = z[0][(size_t) k], zr = z[1][(size_t) k], zm = z[2][(size_t) k], zs = z[3][(size_t) k];
        hLL[(size_t) k] = zl * (zm + zs) * 0.5;
        hRR[(size_t) k] = zr * (zm + zs) * 0.5;
        hLR[(size_t) k] = zr * (zm - zs) * 0.5;   // R -> L
        hRL[(size_t) k] = zl * (zm - zs) * 0.5;   // L -> R
    }

    // Respuesta al impulso centrada en N/2 y con ventana de Hann.
    auto makeIr = [&] (const std::vector<std::complex<double>>& h, float* dest)
    {
        for (int k = 0; k <= half; ++k)
        {
            Cx v ((float) h[(size_t) k].real(), (float) h[(size_t) k].imag());
            if (k == 0 || k == half) v = Cx (v.real(), 0.0f);
            a[(size_t) k] = v;
            if (k > 0 && k < half) a[(size_t) (N - k)] = std::conj (v);
        }
        kernelFft->perform (a.data(), c.data(), true);
        for (int n = 0; n < N; ++n)
        {
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N);
            dest[n] = c[(size_t) ((n + half) % N)].real() * w;
        }
    };

    juce::AudioBuffer<float> direct (2, N), cross (2, N);
    makeIr (hLL, direct.getWritePointer (0));
    makeIr (hRR, direct.getWritePointer (1));
    makeIr (hLR, cross.getWritePointer (0));   // el canal 0 del cruzado recibe R y produce la parte que va a L
    makeIr (hRL, cross.getWritePointer (1));

    convDirect.loadImpulseResponse (std::move (direct), sr, juce::dsp::Convolution::Stereo::yes,
                                    juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    convCross.loadImpulseResponse (std::move (cross), sr, juce::dsp::Convolution::Stereo::yes,
                                   juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    kernelsReady = true;
}

//==============================================================================
// Anchura estéreo y graves en mono (por debajo de una frecuencia el Side se elimina con un paso alto de 24 dB/oct).
void MedidoresEQAudioProcessor::applyWidthAndMono (int n, const Params& p)
{
    const double width = (double) p.width / 100.0;
    const bool mono = p.monoFreq >= 1.0f;
    if (! mono && std::abs (width - 1.0) < 1.0e-4) return;

    if (mono && std::abs (p.monoFreq - lastMonoFreq) > 0.01f)
    {
        monoFilter.c[0] = EQ::rbjHighPass (currentRate, p.monoFreq, 0.5412);
        monoFilter.c[1] = EQ::rbjHighPass (currentRate, p.monoFreq, 1.3066);
        lastMonoFreq = p.monoFreq;
    }

    double* l = ch (0);
    double* r = ch (1);
    for (int i = 0; i < n; ++i)
    {
        const double m = 0.5 * (l[i] + r[i]);
        double sd = 0.5 * (l[i] - r[i]) * width;
        if (mono)   // paso alto de 24 dB/oct sobre el Side: por debajo de la frecuencia solo queda Mid
        {
            for (int st = 0; st < 2; ++st)
            {
                const auto& c = monoFilter.c[st];
                const double y = c.b0 * sd + monoFilter.z1[st];
                monoFilter.z1[st] = c.b1 * sd - c.a1 * y + monoFilter.z2[st];
                monoFilter.z2[st] = c.b2 * sd - c.a2 * y;
                sd = y;
            }
        }
        l[i] = m + sd; r[i] = m - sd;
    }
}

// Saturación después del EQ (las bandas muy subidas "empujan" el saturador, como en un equipo analógico).
//  Cinta:   tanh(a·x)/a, simétrica: armónicos impares, compresión suave.
//  Válvula: tanh(a·x + b) con polarización b, asimétrica: añade armónicos pares (más calidez).
// Ambas tienden a la identidad cuando el Drive tiende a 0 y llevan una compensación parcial de volumen.
// Se calcula a 2x o 4x de la frecuencia de muestreo para que los armónicos no se plieguen.
//
// Latencia: el sobremuestreo retrasa la señal (número entero de muestras, que se informa al host). Para que la latencia sea
// SIEMPRE la misma, también con la saturación apagada o con la mezcla en paralelo, la señal seca pasa por una línea de retardo
// del mismo tamaño: así seco y saturado están alineados y no hay efecto peine.
void MedidoresEQAudioProcessor::saturate (int n, const Params& p)
{
    const float drive = p.drive / 100.0f;
    const float amount = drive * drive * 6.0f;   // "a": 0 = limpio
    const float mix = p.mix / 100.0f;            // 1 = todo saturado, 0 = todo seco
    const bool active = p.character != 0 && amount > 1e-3f && mix > 1e-3f;

    auto& os = osActive == 1 ? os4 : os2;
    if (active && ! satWasActive)
    {
        os.reset();
        for (int c = 0; c < 2; ++c) dcX[c] = dcY[c] = 0.0;
        lastAmount = amount;
    }
    satWasActive = active;

    // Señal seca retardada la misma latencia que la saturada (se hace siempre para que la línea esté al día).
    for (int c = 0; c < 2; ++c)
    {
        const double* in = ch (c);
        double* dry = dryBuf[(size_t) c].data();
        for (int i = 0; i < n; ++i) dry[i] = dryDelay[c].process (in[i], osLatency);
    }

    if (! active)
    {
        for (int c = 0; c < 2; ++c) std::copy_n (dryBuf[(size_t) c].data(), n, ch (c));
        lastAmount = amount;
        return;
    }

    const float bias = 0.3f;
    const float tanhBias = std::tanh (bias);
    const float biasSlope = 1.0f - tanhBias * tanhBias;
    const double dcCoeff = 1.0 - juce::MathConstants<double>::twoPi * 5.0 / currentRate;

    for (int c = 0; c < 2; ++c)
    {
        float* f = satBuf.getWritePointer (c);
        const double* in = ch (c);
        for (int i = 0; i < n; ++i) f[i] = (float) in[i];
    }

    juce::dsp::AudioBlock<float> block (satBuf.getArrayOfWritePointers(), 2, (size_t) n);
    const float a0 = lastAmount, a1 = amount;   // cantidad interpolada dentro del bloque para evitar saltos al mover el Drive
    auto up = os.processSamplesUp (block);
    const int nu = (int) up.getNumSamples();
    for (int c = 0; c < 2; ++c)
    {
        auto* d = up.getChannelPointer ((size_t) c);
        for (int i = 0; i < nu; ++i)
        {
            const float a = juce::jmax (1e-3f, a0 + (a1 - a0) * (float) i / (float) nu);
            const float makeup = std::sqrt (1.0f + a);
            if (p.character == 1)
                d[i] = std::tanh (a * d[i]) / a * makeup;
            else
                d[i] = (std::tanh (a * d[i] + bias) - tanhBias) / (a * biasSlope) * makeup;
        }
    }
    os.processSamplesDown (block);

    for (int c = 0; c < 2; ++c)
    {
        const float* wet = satBuf.getReadPointer (c);
        const double* dry = dryBuf[(size_t) c].data();
        double* out = ch (c);
        for (int i = 0; i < n; ++i)
        {
            double y = (double) wet[i];
            if (p.character == 2)   // la asimetría genera un poco de continua: se quita con un paso alto a ~5 Hz
            {
                const double yy = y - dcX[c] + dcCoeff * dcY[c];
                dcX[c] = y; dcY[c] = yy;
                y = yy;
            }
            out[i] = dry[i] * (1.0 - (double) mix) + y * (double) mix;
        }
    }
    lastAmount = amount;
}

// Solo de banda: se oye solo lo que "toca" la banda (el trozo que recorta un paso alto/bajo, la banda de una campana, etc.).
void MedidoresEQAudioProcessor::runSolo (int b, int n)
{
    EQ::Bq c;
    double f;
    int where = 0;
    if (EQ::isCut (b))
    {
        f = apvts.getRawParameterValue (EQ::freqId (b))->load();
        c = b == EQ::HighPass ? EQ::rbjLowPass (currentRate, f, 0.707) : EQ::rbjHighPass (currentRate, f, 0.707);
    }
    else
    {
        const auto s = EQ::readSettings (b, apvts, currentRate);
        where = EQ::placement (b, apvts);
        c = s.shape == EQ::LowShelfShape || s.shape == EQ::PultecLow || s.shape == EQ::TiltLow || s.shape == EQ::BaxLow
                ? EQ::rbjLowPass (currentRate, s.freq, 0.707)
          : s.shape == EQ::HighShelfShape || s.shape == EQ::PultecHigh || s.shape == EQ::TiltHigh || s.shape == EQ::BaxHigh
                ? EQ::rbjHighPass (currentRate, s.freq, 0.707)
                : EQ::rbjBandPass (currentRate, s.freq, juce::jmax (0.3f, s.q));
    }

    double* l = ch (0);
    double* r = ch (1);
    if (where == 1 || where == 2)
        for (int i = 0; i < n; ++i)
        {
            const double m = 0.5 * (l[i] + r[i]), sd = 0.5 * (l[i] - r[i]);
            l[i] = m; r[i] = sd;
        }

    if (where == 0)
    {
        runBiquad (l, n, c, soloZ1[0], soloZ2[0]);
        runBiquad (r, n, c, soloZ1[1], soloZ2[1]);
    }
    else
    {
        const int chan = where <= 2 ? where - 1 : where - 3;
        runBiquad (ch (chan), n, c, soloZ1[chan], soloZ2[chan]);
        const double* y = ch (chan);
        for (int i = 0; i < n; ++i)
        {
            const double v = y[i];
            l[i] = v;
            r[i] = where == 2 ? -v : v;
        }
    }
}

//==============================================================================
// RMS suavizado (~300 ms) de los dos canales.
void MedidoresEQAudioProcessor::updateRms (double* ms, std::atomic<float>* out, const double* l, const double* r, int n)
{
    double sumL = 0.0, sumR = 0.0;
    for (int i = 0; i < n; ++i) { sumL += l[i] * l[i]; sumR += r[i] * r[i]; }
    const double a = std::exp (-(double) n / (0.3 * currentRate));
    ms[0] = ms[0] * a + (sumL / (double) n) * (1.0 - a);
    ms[1] = ms[1] * a + (sumR / (double) n) * (1.0 - a);
    out[0].store ((float) std::sqrt (ms[0]));
    out[1].store ((float) std::sqrt (ms[1]));
}

void MedidoresEQAudioProcessor::processChunk (int n, const Params& p)
{
    double* l = ch (0);
    double* r = ch (1);
    const double* rl = raw[0].data();
    const double* rr = raw[1].data();

    // Ganancia de entrada antes de todo; el medidor de entrada mide ya con ella aplicada (lo que llega al EQ).
    inGainSm.setTargetValue (juce::Decibels::decibelsToGain ((double) p.inDb));
    float pkL = 0.0f, pkR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double g = inGainSm.getNextValue();
        l[i] = rl[i] * g; r[i] = rr[i] * g;
        pkL = juce::jmax (pkL, (float) std::abs (l[i])); pkR = juce::jmax (pkR, (float) std::abs (r[i]));
    }
    inPeak[0].store (juce::jmax (inPeak[0].load(), pkL));
    inPeak[1].store (juce::jmax (inPeak[1].load(), pkR));
    inLoud.process (rl, rr, n);
    updateRms (inMs, inRms, l, r, n);
    if (p.analyzer == 2) pushAnalyzerSamples (l, r, n);

    // El original, retardado lo mismo que el procesado, para el bypass.
    const int totalLatency = osLatency + (phaseMode > 0 ? firLength / 2 : 0);
    for (int i = 0; i < n; ++i)
    {
        scratch[0][(size_t) i] = bypassDelay[0].process (rl[i], totalLatency);
        scratch[1][(size_t) i] = bypassDelay[1].process (rr[i], totalLatency);
    }

    if (p.solo > 0)
    {
        runSolo (p.solo - 1, n);
    }
    else
    {
        for (int b = 0; b < EQ::NumBands; ++b)
        {
            if (phaseMode > 0 && ! band[b].dyn)
            {
                if (EQ::hasDyn (b)) dynGainDb[b].store (0.0f);
                continue;   // esta banda va en el filtro FIR
            }
            processBand (b, n);
        }
        if (phaseMode > 0) runFir (n);
        applyWidthAndMono (n, p);
        saturate (n, p);
    }

    outGainSm.setTargetValue (juce::Decibels::decibelsToGain ((double) p.outDb));
    for (int i = 0; i < n; ++i)
    {
        const double g = outGainSm.getNextValue();
        l[i] *= g; r[i] *= g;
    }

    // Igualar volumen: el procesado se compensa para sonar igual de fuerte (loudness, ventana de 3 s) que el original.
    procLoud.process (l, r, n);
    double autoTarget = 0.0;
    if (p.autoGain)
    {
        const float in = inLoud.shortTerm(), pr = procLoud.shortTerm();
        if (in > -70.0f && pr > -70.0f) autoTarget = juce::jlimit (-12.0, 12.0, (double) (in - pr));
        else autoTarget = autoGainState;
    }
    if (std::abs (autoTarget - autoGainState) > 1.0e-3)
    {
        autoGainState = autoTarget;
        autoSm.setTargetValue (juce::Decibels::decibelsToGain (autoGainState));
    }
    autoGainDb.store ((float) autoGainState);

    bypassSm.setTargetValue (p.bypass ? 1.0 : 0.0);
    for (int i = 0; i < n; ++i)
    {
        const double ag = autoSm.getNextValue();
        const double bp = bypassSm.getNextValue();
        l[i] = l[i] * ag * (1.0 - bp) + scratch[0][(size_t) i] * bp;
        r[i] = r[i] * ag * (1.0 - bp) + scratch[1][(size_t) i] * bp;
    }

    // Dither TPDF antes de pasar a coma flotante de 32 bits (solo si el host trabaja a 16 o 24 bits).
    if (p.dither > 0)
    {
        const double lsb = std::pow (2.0, 1.0 - (p.dither == 1 ? 16.0 : 24.0));
        for (int i = 0; i < n; ++i)
        {
            l[i] += (randomUnit() - randomUnit()) * lsb;
            r[i] += (randomUnit() - randomUnit()) * lsb;
        }
    }

    // Medición de la salida
    pkL = pkR = 0.0f;
    const double cc = std::exp (-1.0 / (0.3 * currentRate));
    for (int i = 0; i < n; ++i)
    {
        pkL = juce::jmax (pkL, (float) std::abs (l[i])); pkR = juce::jmax (pkR, (float) std::abs (r[i]));
        corrLR = corrLR * cc + l[i] * r[i] * (1.0 - cc);
        corrLL = corrLL * cc + l[i] * l[i] * (1.0 - cc);
        corrRR = corrRR * cc + r[i] * r[i] * (1.0 - cc);
    }
    updateRms (outMs, outRms, l, r, n);
    outPeak[0].store (juce::jmax (outPeak[0].load(), pkL));
    outPeak[1].store (juce::jmax (outPeak[1].load(), pkR));
    const double denom = std::sqrt (corrLL * corrRR);
    correlation.store (denom > 1.0e-10 ? (float) juce::jlimit (-1.0, 1.0, corrLR / denom) : 1.0f);

    outLoud.process (l, r, n);
    truePeak.process (l, r, n);

    // Goniómetro: un punto de cada 4 muestras
    const int free = gonioFifo.getFreeSpace();
    if (free > 0)
    {
        const int pairs = juce::jmin (free, (n + 3) / 4);
        int written = 0;
        auto scope = gonioFifo.write (pairs);
        auto put = [&] (int startIndex, int size)
        {
            for (int k = 0; k < size; ++k)
            {
                const int src = juce::jmin (n - 1, (written++) * 4);
                gonioData[(size_t) (startIndex + k) * 2]     = (float) l[src];
                gonioData[(size_t) (startIndex + k) * 2 + 1] = (float) r[src];
            }
        };
        put (scope.startIndex1, scope.blockSize1);
        put (scope.startIndex2, scope.blockSize2);
    }

    if (p.analyzer == 1) pushAnalyzerSamples (l, r, n);
}

void MedidoresEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();

    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);
    const int nch = juce::jmin (2, mainIn.getNumChannels());
    if (nch < 1 || total < 1) return;

    // Sidechain externo (si el host lo ofrece)
    const float* scPtr[2] { nullptr, nullptr };
    scAvailable = false;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            scPtr[0] = sc.getReadPointer (0);
            scPtr[1] = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scPtr[0];
            scAvailable = true;
        }
    }

    // Parámetros del bloque
    auto read = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    Params p;
    p.character = (int) read (EQ::characterId);
    p.phase = (int) read (EQ::phaseId);
    p.dither = (int) read (EQ::ditherId);
    p.solo = (int) read (EQ::soloId);
    p.analyzer = (int) read (EQ::analyzerId);
    p.osChoice = (int) read (EQ::osId);
    p.drive = read (EQ::driveId);
    p.mix = read (EQ::mixId);
    p.monoFreq = read (EQ::monoFreqId);
    p.width = read (EQ::widthId);
    p.inDb = read (EQ::inId);
    p.outDb = read (EQ::outId);
    p.bypass = read (EQ::bypassId) > 0.5f;
    p.autoGain = read (EQ::autoGainId) > 0.5f;
    p.extSc = read (EQ::scId) > 0.5f;
    p.rms = read (EQ::detId) > 0.5f;
    useExtSc = p.extSc && scAvailable;
    detRms = p.rms;

    if (p.phase != phaseMode)
    {
        if (p.phase > 0 && phaseMode == 0) { convDirect.reset(); convCross.reset(); }
        phaseMode = p.phase;
    }
    if ((p.osChoice == 1 ? 1 : 0) != osActive)
    {
        osActive = p.osChoice == 1 ? 1 : 0;
        osLatency = osActive == 1 ? osLatency4 : osLatency2;
        os2.reset(); os4.reset();
        for (auto& d : dryDelay) d.reset();
        satWasActive = false;
    }

    updateTargets (false);

    for (int start = 0; start < total; start += maxBlockSize)
    {
        const int n = juce::jmin (maxBlockSize, total - start);

        const float* in0 = mainIn.getReadPointer (0) + start;
        const float* in1 = nch > 1 ? mainIn.getReadPointer (1) + start : in0;
        for (int i = 0; i < n; ++i) { raw[0][(size_t) i] = in0[i]; raw[1][(size_t) i] = in1[i]; }
        if (scAvailable)
            for (int i = 0; i < n; ++i) { scBuf[0][(size_t) i] = scPtr[0][start + i]; scBuf[1][(size_t) i] = scPtr[1][start + i]; }

        processChunk (n, p);

        float* o0 = mainOut.getWritePointer (0) + start;
        for (int i = 0; i < n; ++i) o0[i] = (float) work[0][(size_t) i];
        if (nch > 1)
        {
            float* o1 = mainOut.getWritePointer (1) + start;
            for (int i = 0; i < n; ++i) o1[i] = (float) work[1][(size_t) i];
        }
    }
}

//==============================================================================
void MedidoresEQAudioProcessor::pushAnalyzerSamples (const double* l, const double* r, int n)
{
    const int count = juce::jmin (n, analyzerFifo.getFreeSpace());
    if (count <= 0) return;

    int src = 0;
    auto scope = analyzerFifo.write (count);
    auto copy = [&] (int start, int size)
    {
        for (int i = 0; i < size; ++i, ++src)
            analyzerData[(size_t) (start + i)] = (float) (0.5 * (l[src] + r[src]));
    };
    copy (scope.startIndex1, scope.blockSize1);
    copy (scope.startIndex2, scope.blockSize2);
}

int MedidoresEQAudioProcessor::pullAnalyzerSamples (float* dest, int maxSamples)
{
    const int n = juce::jmin (maxSamples, analyzerFifo.getNumReady());
    if (n <= 0) return 0;

    auto scope = analyzerFifo.read (n);
    std::copy_n (analyzerData.begin() + scope.startIndex1, scope.blockSize1, dest);
    std::copy_n (analyzerData.begin() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
    return n;
}

int MedidoresEQAudioProcessor::pullGoniometer (float* dest, int maxPairs)
{
    const int n = juce::jmin (maxPairs, gonioFifo.getNumReady());
    if (n <= 0) return 0;

    auto scope = gonioFifo.read (n);
    std::copy_n (gonioData.begin() + scope.startIndex1 * 2, scope.blockSize1 * 2, dest);
    std::copy_n (gonioData.begin() + scope.startIndex2 * 2, scope.blockSize2 * 2, dest + scope.blockSize1 * 2);
    return n;
}

MedidoresEQAudioProcessor::Loudness MedidoresEQAudioProcessor::getLoudness() const
{
    return { outLoud.momentary(), outLoud.shortTerm(), outLoud.integrated(),
             juce::Decibels::gainToDecibels (truePeak.peakLinear(), -100.0f) };
}

void MedidoresEQAudioProcessor::resetLoudness()
{
    outLoud.requestReset();
    truePeak.requestReset();
}

//==============================================================================
// Estado: los ajustes actuales más las cuatro ranuras A/B/C/D.
void MedidoresEQAudioProcessor::switchSlot (int slot)
{
    slot = juce::jlimit (0, 3, slot);
    if (slot == activeSlot) return;

    if (auto xml = apvts.copyState().createXml()) slotXml[activeSlot] = xml->toString();
    activeSlot = slot;

    // Una ranura vacía se queda con los ajustes actuales (sirve para copiar A a B y retocar).
    if (slotXml[slot].isNotEmpty())
        if (auto xml = juce::parseXML (slotXml[slot]))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void MedidoresEQAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto root = std::make_unique<juce::XmlElement> ("ECU10MST");
    root->setAttribute ("active", activeSlot);
    if (auto xml = apvts.copyState().createXml())
        root->addChildElement (xml.release());

    for (int i = 0; i < 4; ++i)
        if (i != activeSlot && slotXml[i].isNotEmpty())
            if (auto xml = juce::parseXML (slotXml[i]))
            {
                auto* slot = root->createNewChildElement ("SLOT");
                slot->setAttribute ("n", i);
                slot->addChildElement (xml.release());
            }
    copyXmlToBinary (*root, dest);
}

void MedidoresEQAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;

    if (xml->hasTagName ("ECU10MST"))
    {
        activeSlot = juce::jlimit (0, 3, xml->getIntAttribute ("active", 0));
        for (auto& s : slotXml) s = {};
        for (auto* child : xml->getChildIterator())
        {
            if (child->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*child));
            else if (child->hasTagName ("SLOT"))
                if (auto* inner = child->getFirstChildElement())
                    slotXml[juce::jlimit (0, 3, child->getIntAttribute ("n", 0))] = inner->toString();
        }
    }
    else if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MedidoresEQAudioProcessor::createEditor()
{
    return new MedidoresEQAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MedidoresEQAudioProcessor();
}
