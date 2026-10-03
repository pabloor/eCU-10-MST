// Pruebas funcionales del procesado: respuesta de cada modo de fase, canales Mid/Side/L/R, graves en mono, anchura,
// dinámica (compresión y expansión), solo de banda, medición (LUFS, true peak, correlación) y cambio de ranuras A/B.
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setParam (MedidoresEQAudioProcessor& p, const juce::String& id, float value)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

static int failures = 0;
static void check (const char* name, bool ok, double value = 0.0)
{
    std::printf ("%-58s %s  (%.3f)\n", name, ok ? "OK" : "FALLO", value);
    if (! ok) ++failures;
}

struct Rig
{
    static constexpr double sr = 48000.0;
    static constexpr int block = 512;
    MedidoresEQAudioProcessor proc;

    void start()
    {
        proc.setRateAndBufferSizeDetails (sr, block);
        proc.prepareToPlay (sr, block);
        proc.rebuildKernels();
        for (int i = 0; i < 40; ++i) { run ([] (int, int) { return 0.0f; }, [] (int, int) { return 0.0f; }, 1); juce::Thread::sleep (15); }
    }

    // Procesa "blocks" bloques con la señal dada por funciones (canal, índice global) y devuelve la salida
    template <typename FL, typename FR>
    std::array<std::vector<float>, 2> run (FL fl, FR fr, int blocks, int firstIndex = 0)
    {
        std::array<std::vector<float>, 2> out;
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf (2, block);
            for (int i = 0; i < block; ++i)
            {
                buf.setSample (0, i, fl (0, firstIndex + b * block + i));
                buf.setSample (1, i, fr (1, firstIndex + b * block + i));
            }
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i) out[(size_t) ch].push_back (buf.getSample (ch, i));
        }
        return out;
    }
};

static double rmsOf (const std::vector<float>& v, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (to - from));
}
static double dbOf (double x) { return 20.0 * std::log10 (juce::jmax (x, 1.0e-9)); }

// Ganancia en dB de la salida respecto a la entrada (seno de amplitud 0,1 en las dos entradas, con fase relativa dada)
static std::pair<double, double> gainDb (Rig& rig, double freq, double rightSign, int blocks = 100)
{
    const double amp = 0.1;
    auto sine = [&] (double sign) { return [=] (int, int n) { return (float) (sign * amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * n / Rig::sr)); }; };
    auto out = rig.run (sine (1.0), sine (rightSign), blocks);
    const size_t to = out[0].size(), from = to - 8192;
    const double ref = amp / std::sqrt (2.0);
    return { dbOf (rmsOf (out[0], from, to) / ref), dbOf (rmsOf (out[1], from, to) / ref) };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const char* modeName[] = { "minima", "natural", "lineal" };

    // 1. Una campana de +6 dB a 1 kHz da +6 dB en cada modo de fase, y deja pasar otras frecuencias
    for (int mode = 0; mode < 3; ++mode)
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        setParam (rig.proc, "b2_freq", 1000.0f); setParam (rig.proc, "b2_gain", 6.0f); setParam (rig.proc, "b2_q", 1.0f);
        setParam (rig.proc, "style", 0.0f);
        setParam (rig.proc, "phase", (float) mode);
        rig.start();
        const auto at1k = gainDb (rig, 1000.0, 1.0);
        const auto at100 = gainDb (rig, 100.0, 1.0);
        const auto at10k = gainDb (rig, 10000.0, 1.0);
        char name[96];
        std::snprintf (name, sizeof name, "campana +6 dB a 1 kHz, fase %s", modeName[mode]);
        check (name, std::abs (at1k.first - 6.0) < 0.3 && std::abs (at1k.second - 6.0) < 0.3, at1k.first);
        std::snprintf (name, sizeof name, "  y casi plana a 100 Hz y 10 kHz, fase %s", modeName[mode]);
        check (name, std::abs (at100.first) < 0.8 && std::abs (at10k.first) < 0.8, at100.first);
    }

    // 2. Banda solo en el canal izquierdo / derecho / Mid / Side, en fase mínima y lineal
    for (int mode : { 0, 2 })
        for (int ch = 1; ch <= 4; ++ch)
        {
            Rig rig;
            setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
            setParam (rig.proc, "b2_freq", 1000.0f); setParam (rig.proc, "b2_gain", 6.0f); setParam (rig.proc, "style", 0.0f);
            setParam (rig.proc, "b2_ch", (float) ch);
            setParam (rig.proc, "phase", (float) mode);
            rig.start();
            // Mid = señal igual en L y R; Side = señal opuesta
            const bool sideTest = ch == 2;
            const auto g = gainDb (rig, 1000.0, sideTest ? -1.0 : 1.0);
            char name[96];
            std::snprintf (name, sizeof name, "banda en %s, fase %s", ch == 1 ? "Mid" : ch == 2 ? "Side" : ch == 3 ? "L" : "R", modeName[mode]);
            const double wantL = ch == 3 ? 6.0 : ch == 4 ? 0.0 : 6.0, wantR = ch == 3 ? 0.0 : 6.0;
            // En L solo sube L; en R solo sube R; en Mid/Side la señal de prueba adecuada sube en los dos canales
            const bool ok = std::abs (g.first - (ch == 4 ? 0.0 : wantL)) < 0.4 && std::abs (g.second - (ch == 3 ? 0.0 : wantR)) < 0.4;
            check (name, ok, g.first);
        }

    // 3. Mid no toca el Side y al revés
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        setParam (rig.proc, "b2_freq", 1000.0f); setParam (rig.proc, "b2_gain", 6.0f); setParam (rig.proc, "style", 0.0f);
        setParam (rig.proc, "b2_ch", 1.0f);
        rig.start();
        const auto side = gainDb (rig, 1000.0, -1.0);
        check ("banda en Mid no cambia una señal solo Side", std::abs (side.first) < 0.3, side.first);
    }

    // 4. Graves en mono y anchura
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        setParam (rig.proc, "mono_freq", 150.0f);
        rig.start();
        const auto low = gainDb (rig, 40.0, -1.0);
        const auto high = gainDb (rig, 5000.0, -1.0);
        check ("graves en mono: el Side de 40 Hz desaparece", low.first < -20.0, low.first);
        check ("graves en mono: el Side de 5 kHz se conserva", std::abs (high.first) < 0.5, high.first);
    }
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        setParam (rig.proc, "width", 0.0f);
        rig.start();
        const auto g = gainDb (rig, 1000.0, -1.0);
        check ("anchura 0 %: una señal Side desaparece", g.first < -40.0, g.first);
        setParam (rig.proc, "width", 200.0f);
        const auto w = gainDb (rig, 1000.0, -1.0);
        check ("anchura 200 %: el Side sube 6 dB", std::abs (w.first - 6.0) < 0.3, w.first);
    }

    // 5. Dinámica: compresión (actúa con señal fuerte) y expansión (actúa con señal débil), RMS y pico
    for (int det = 0; det < 2; ++det)
        for (int mode = 0; mode < 2; ++mode)
        {
            Rig rig;
            setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
            setParam (rig.proc, "b2_freq", 1000.0f); setParam (rig.proc, "b2_gain", -9.0f); setParam (rig.proc, "style", 0.0f);
            setParam (rig.proc, "b2_dyn", 1.0f); setParam (rig.proc, "b2_thr", -30.0f); setParam (rig.proc, "b2_ratio", 10.0f);
            setParam (rig.proc, "b2_attack", 1.0f); setParam (rig.proc, "b2_release", 50.0f);
            setParam (rig.proc, "b2_dmode", (float) mode); setParam (rig.proc, "dyn_det", (float) det);
            rig.start();
            const double quiet = 0.003, loud = 0.3;   // -50 dBFS y -10 dBFS
            auto level = [&] (double amp)
            {
                auto s = [=] (int, int n) { return (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / Rig::sr)); };
                auto out = rig.run (s, s, 60);
                return dbOf (rmsOf (out[0], out[0].size() - 8192, out[0].size()) / (amp / std::sqrt (2.0)));
            };
            const double gq = level (quiet), gl = level (loud);
            char name[96];
            std::snprintf (name, sizeof name, "dinamica %s, detector %s", mode == 0 ? "compresion" : "expansion", det == 0 ? "pico" : "RMS");
            const bool ok = mode == 0 ? (std::abs (gq) < 1.0 && gl < -5.0) : (gq < -5.0 && std::abs (gl) < 1.0);
            check (name, ok, mode == 0 ? gl : gq);
        }

    // 6. Solo de banda: con el solo en una campana de 1 kHz se oye 1 kHz y no 100 Hz
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        setParam (rig.proc, "b2_freq", 1000.0f); setParam (rig.proc, "b2_q", 2.0f);
        setParam (rig.proc, "solo", 4.0f);   // 0 = ninguna, 1 = paso alto, 2 = graves, 3 = medio 1, 4 = medio 2
        rig.start();
        const auto on = gainDb (rig, 1000.0, 1.0), off = gainDb (rig, 100.0, 1.0);
        check ("solo de banda: pasa su frecuencia", std::abs (on.first) < 1.0, on.first);
        check ("solo de banda: no pasa los graves", off.first < -12.0, off.first);
    }

    // 7. Medición: LUFS de un seno de -20 dBFS en cada canal, true peak y correlación
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        rig.start();
        const auto g = gainDb (rig, 1000.0, 1.0, 400);   // ~4 s
        (void) g;
        const auto l = rig.proc.getLoudness();
        check ("LUFS momentaneo de un seno de -20 dBFS en cada canal = -20.0", std::abs (l.momentary + 20.0) < 0.3, l.momentary);
        check ("LUFS corto plazo = -20.0", std::abs (l.shortTerm + 20.0) < 0.3, l.shortTerm);
        check ("LUFS integrado = -20.0", std::abs (l.integrated + 20.0) < 0.4, l.integrated);
        check ("true peak ~ -20 dBTP", std::abs (l.truePeakDb + 20.0) < 0.5, l.truePeakDb);
        check ("correlacion de una senal mono = +1", rig.proc.getCorrelation() > 0.99f, rig.proc.getCorrelation());
        gainDb (rig, 1000.0, -1.0, 300);
        check ("correlacion de una senal en contrafase = -1", rig.proc.getCorrelation() < -0.99f, rig.proc.getCorrelation());
    }

    // 8. Igualar volumen: un EQ que sube 6 dB baja el nivel hasta igualar el original
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        setParam (rig.proc, "out_gain", 6.0f); setParam (rig.proc, "autogain", 1.0f);
        rig.start();
        const auto g = gainDb (rig, 1000.0, 1.0, 1500);   // ~16 s para que se asiente
        check ("igualar volumen compensa +6 dB de salida", std::abs (g.first) < 0.8, g.first);
        check ("  la compensacion informada es de -6 dB", std::abs (rig.proc.getAutoGainDb() + 6.0f) < 0.8f, rig.proc.getAutoGainDb());
    }

    // 9. Ranuras A/B: se conservan los ajustes de cada una
    {
        Rig rig;
        rig.start();
        setParam (rig.proc, "in_gain", 3.0f);
        rig.proc.switchSlot (1);
        setParam (rig.proc, "in_gain", -4.0f);
        rig.proc.switchSlot (0);
        const float a = rig.proc.apvts.getRawParameterValue ("in_gain")->load();
        rig.proc.switchSlot (1);
        const float b = rig.proc.apvts.getRawParameterValue ("in_gain")->load();
        check ("ranuras A/B guardan ajustes distintos", std::abs (a - 3.0f) < 0.05f && std::abs (b + 4.0f) < 0.05f, a);

        juce::MemoryBlock state;
        rig.proc.getStateInformation (state);
        MedidoresEQAudioProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        check ("el estado guardado recupera la ranura activa", other.getActiveSlot() == 1, other.getActiveSlot());
        other.switchSlot (0);
        check ("  y la otra ranura", std::abs (other.apvts.getRawParameterValue ("in_gain")->load() - 3.0f) < 0.05f);
    }


    // 10. Sidechain externo: la dinámica reacciona a la entrada de sidechain, no a la señal principal
    {
        for (int external = 0; external < 2; ++external)
        {
            MedidoresEQAudioProcessor proc;
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            const bool accepted = proc.setBusesLayout (layout);
            if (external == 0) check ("el plugin acepta la entrada de sidechain", accepted);
            setParam (proc, "hp_on", 0.0f); setParam (proc, "lp_on", 0.0f);
            setParam (proc, "b2_freq", 1000.0f); setParam (proc, "b2_gain", -9.0f); setParam (proc, "style", 0.0f);
            setParam (proc, "b2_dyn", 1.0f); setParam (proc, "b2_thr", -30.0f); setParam (proc, "b2_ratio", 10.0f);
            setParam (proc, "b2_attack", 1.0f); setParam (proc, "b2_release", 50.0f);
            setParam (proc, "dyn_sc", (float) external);
            proc.setRateAndBufferSizeDetails (Rig::sr, Rig::block);
            proc.prepareToPlay (Rig::sr, Rig::block);

            // Principal: seno flojo (-50 dBFS) a 1 kHz. Sidechain: seno fuerte (-10 dBFS) a 1 kHz.
            std::vector<float> out;
            for (int b = 0; b < 60; ++b)
            {
                juce::AudioBuffer<float> buf (4, Rig::block);
                for (int i = 0; i < Rig::block; ++i)
                {
                    const double ph = 2.0 * juce::MathConstants<double>::pi * 1000.0 * (b * Rig::block + i) / Rig::sr;
                    buf.setSample (0, i, (float) (0.003 * std::sin (ph))); buf.setSample (1, i, (float) (0.003 * std::sin (ph)));
                    buf.setSample (2, i, (float) (0.3 * std::sin (ph)));   buf.setSample (3, i, (float) (0.3 * std::sin (ph)));
                }
                juce::MidiBuffer midi;
                proc.processBlock (buf, midi);
                for (int i = 0; i < Rig::block; ++i) out.push_back (buf.getSample (0, i));
            }
            const double g = dbOf (rmsOf (out, out.size() - 8192, out.size()) / (0.003 / std::sqrt (2.0)));
            check (external == 0 ? "detector interno: la señal floja no activa la dinamica"
                                 : "detector externo: el sidechain fuerte activa la dinamica en la señal floja",
                   external == 0 ? std::abs (g) < 1.0 : g < -5.0, g);
        }
    }

    // 11. Botones de solo del editor: reciben el clic, activan el solo y se marcan
    {
        MedidoresEQAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (Rig::sr, Rig::block);
        proc.prepareToPlay (Rig::sr, Rig::block);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);
        bool allOk = true;
        for (int b = 0; b < EQ::NumBands; ++b)
        {
            auto* btn = dynamic_cast<juce::Button*> (editor->findChildWithID ("solo" + juce::String (b)));
            if (btn == nullptr) { allOk = false; continue; }
            const auto centre = editor->getLocalPoint (btn, btn->getLocalBounds().getCentre());
            const bool reachable = editor->getComponentAt (centre) == btn;   // nada tapa el botón
            btn->onClick();   // lo que ejecuta el clic (triggerClick es asíncrono)
            const bool engaged = (int) proc.apvts.getRawParameterValue ("solo")->load() == b + 1 && btn->getToggleState();
            btn->onClick();   // segundo clic: lo apaga
            const bool released = (int) proc.apvts.getRawParameterValue ("solo")->load() == 0 && ! btn->getToggleState();
            if (! (reachable && engaged && released)) { allOk = false;
                auto* at = editor->getComponentAt (centre);
                std::printf ("  btn bounds %d,%d %dx%d visible=%d showing=%d enabled=%d centre=%d,%d  en el punto: %s (%d,%d %dx%d) solo=%d\n", btn->getX(), btn->getY(), btn->getWidth(), btn->getHeight(), btn->isVisible(), btn->isShowing(), btn->isEnabled(), centre.x, centre.y,
                             at ? at->getComponentID().toRawUTF8() : "-", at ? at->getX() : 0, at ? at->getY() : 0, at ? at->getWidth() : 0, at ? at->getHeight() : 0, (int) proc.apvts.getRawParameterValue ("solo")->load()); std::printf ("  solo banda %d: alcanzable=%d activa=%d apaga=%d\n", b, reachable, engaged, released); }
        }
        check ("botones S del editor: clic alcanza, activa, marca y apaga", allOk);

        // Zoom de la interfaz: el parámetro escala el editor sin cambiar su tamaño lógico
        auto* zoom = proc.apvts.getParameter (EQ::scaleId);
        zoom->setValueNotifyingHost (zoom->convertTo0to1 (2.0f));   // 125 %
        const float sx = editor->getTransform().mat00;
        check ("zoom 125 %: el editor se escala y conserva el tamaño lógico",
               std::abs (sx - 1.25f) < 1.0e-4f && editor->getWidth() == 1280, sx);
        zoom->setValueNotifyingHost (zoom->convertTo0to1 (1.0f));
    }

    // 12. Delta: solo se oye la diferencia entre el procesado y el original
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        setParam (rig.proc, "delta", 1.0f);
        rig.start();
        const auto flat = gainDb (rig, 1000.0, 1.0);
        check ("delta con el EQ plano: silencio", flat.first < -80.0, flat.first);
        setParam (rig.proc, "b2_freq", 1000.0f); setParam (rig.proc, "b2_gain", 6.0f); setParam (rig.proc, "style", 0.0f);
        const auto bell = gainDb (rig, 1000.0, 1.0);
        check ("delta de una campana +6 dB: la diferencia (x2 - x) = 0 dB", std::abs (bell.first) < 0.4, bell.first);
        setParam (rig.proc, "delta", 0.0f);
        const auto normal = gainDb (rig, 1000.0, 1.0);
        check ("delta apagado: vuelve el sonido normal (+6 dB)", std::abs (normal.first - 6.0) < 0.4, normal.first);
    }
    {
        // Lo mismo en fase lineal (la diferencia se calcula con el original retardado la misma latencia)
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        setParam (rig.proc, "delta", 1.0f); setParam (rig.proc, "phase", 2.0f);
        rig.start();
        const auto flat = gainDb (rig, 1000.0, 1.0);
        check ("delta con el EQ plano en fase lineal: silencio", flat.first < -60.0, flat.first);
    }

    // 13. Utilidades de monitorizacion
    {
        auto sineAt = [] (double amp) { return [=] (int, int n) { return (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / Rig::sr)); }; };
        auto silence = [] (int, int) { return 0.0f; };
        auto dot = [] (const std::array<std::vector<float>, 2>& o)
        {
            double d = 0.0;
            for (size_t i = o[0].size() - 8192; i < o[0].size(); ++i) d += (double) o[0][i] * o[1][i];
            return d;
        };

        {   // mono: una señal solo en L sale igual por los dos canales, 6 dB más baja
            Rig rig;
            setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f); setParam (rig.proc, "mon_mono", 1.0f);
            rig.start();
            auto out = rig.run (sineAt (0.2), silence, 80);
            const double l = dbOf (rmsOf (out[0], out[0].size() - 8192, out[0].size()) / (0.2 / std::sqrt (2.0)));
            const double r = dbOf (rmsOf (out[1], out[1].size() - 8192, out[1].size()) / (0.2 / std::sqrt (2.0)));
            check ("monitor mono: L-solo sale por los dos canales a -6 dB", std::abs (l + 6.0) < 0.3 && std::abs (r + 6.0) < 0.3, l);
        }
        {   // intercambio L/R
            Rig rig;
            setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f); setParam (rig.proc, "mon_swap", 1.0f);
            rig.start();
            auto out = rig.run (sineAt (0.2), silence, 80);
            const double l = rmsOf (out[0], out[0].size() - 8192, out[0].size()), r = rmsOf (out[1], out[1].size() - 8192, out[1].size());
            check ("monitor swap: la señal de L aparece en R", l < 1.0e-6 && r > 0.1, r);
        }
        {   // polaridad: con L y R iguales, invertir uno los deja en contrafase
            Rig rig;
            setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f); setParam (rig.proc, "mon_pol_l", 1.0f);
            rig.start();
            auto out = rig.run (sineAt (0.2), sineAt (0.2), 80);
            check ("monitor polaridad L: los canales quedan en contrafase", dot (out) < 0.0, dot (out));
            setParam (rig.proc, "mon_pol_l", 0.0f); setParam (rig.proc, "mon_pol_r", 1.0f);
            out = rig.run (sineAt (0.2), sineAt (0.2), 80);
            check ("monitor polaridad R: lo mismo", dot (out) < 0.0, dot (out));
            setParam (rig.proc, "mon_pol_r", 0.0f);
            out = rig.run (sineAt (0.2), sineAt (0.2), 80);
            check ("sin inversion: en fase otra vez", dot (out) > 0.0, dot (out));
        }
    }

    // 14. Filtro de continua
    for (int dc = 0; dc < 2; ++dc)
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f); setParam (rig.proc, "dc_filter", (float) dc);
        rig.start();
        auto s = [] (int, int n) { return (float) (0.1 + 0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / Rig::sr)); };
        auto out = rig.run (s, s, 400);
        double mean = 0.0;
        for (size_t i = out[0].size() - 8192; i < out[0].size(); ++i) mean += out[0][i];
        mean /= 8192.0;
        check (dc ? "filtro DC activado: la continua desaparece" : "filtro DC apagado: la continua pasa", dc ? std::abs (mean) < 0.003 : std::abs (mean - 0.1) < 0.003, mean);
    }

    // 15. Cambio de fase, calidad y sobremuestreo con el audio en marcha: sin valores raros, con fundido y alineado después
    {
        Rig rig;
        setParam (rig.proc, "hp_on", 0.0f); setParam (rig.proc, "lp_on", 0.0f);
        rig.start();
        juce::Random random (42);
        auto noise = [&random] (int, int) { return 0.2f * (random.nextFloat() * 2.0f - 1.0f); };
        auto out = rig.run (noise, noise, 20);
        setParam (rig.proc, "phase", 2.0f);
        setParam (rig.proc, "phase_quality", 0.0f);
        setParam (rig.proc, "os", 1.0f);
        rig.proc.refreshLatency();
        rig.proc.rebuildKernels();
        float peak = 0.0f; bool finite = true; float minGain = 1.0f;
        for (int round = 0; round < 60; ++round)
        {
            auto o = rig.run (noise, noise, 2);
            for (auto v : o[0]) { peak = juce::jmax (peak, std::abs (v)); if (! std::isfinite (v)) finite = false; }
            juce::Thread::sleep (10);
            for (size_t w = 0; w + 64 <= o[0].size(); w += 64) minGain = juce::jmin (minGain, (float) rmsOf (o[0], w, w + 64));
        }
        check ("cambio de configuracion en marcha: valores validos y sin saltos", finite && peak < 0.45f, peak);
        check ("  el fundido llega a silenciar el cambio", minGain < 0.02f, minGain);

        // Alineación tras el cambio: un impulso sale justo en la latencia informada
        const int latency = rig.proc.getLatencySamples();
        rig.proc.rebuildKernels();   // en el plugin lo hace el temporizador tras el cambio de calidad
        for (int i = 0; i < 20; ++i) { rig.run ([] (int, int) { return 0.0f; }, [] (int, int) { return 0.0f; }, 1); juce::Thread::sleep (10); }
        auto o = rig.run ([] (int, int n) { return n == 100 ? 0.1f : 0.0f; }, [] (int, int n) { return n == 100 ? 0.1f : 0.0f; }, (latency + 4096) / Rig::block + 3);
        size_t pk = 0;
        for (size_t i = 0; i < o[0].size(); ++i) if (std::abs (o[0][i]) > std::abs (o[0][pk])) pk = i;
        check ("tras el cambio: el impulso sale en la latencia informada", (int) pk - 100 == latency, (double) ((int) pk - 100));
    }

    std::printf ("%s\n", failures == 0 ? "Todas las pruebas funcionales correctas" : "HAY PRUEBAS FALLIDAS");
    return failures == 0 ? 0 : 1;
}
