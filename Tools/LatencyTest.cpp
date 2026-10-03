// Mide la latencia real del plugin con un impulso y la compara con la que informa al host.
// Con el EQ plano, la latencia debe ser la misma con la saturación apagada, con cinta, con válvula, con mezclas en paralelo,
// en cada modo de fase, con sobremuestreo 2x y 4x, con el bypass y a cualquier frecuencia de muestreo (hasta 192 kHz).
#include "PluginProcessor.h"

static void setParam (MedidoresEQAudioProcessor& p, const juce::String& id, float value)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

struct Case { const char* name; double sampleRate; float character, drive, mix, phase, os, bypass; bool checkGain; };

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const int blockSize = 512, impulseAt = 100;

    const Case cases[] = {
        { "limpio 48k",                48000.0, 0.0f,  0.0f, 100.0f, 0.0f, 0.0f, 0.0f, true  },
        { "cinta drive 0",             48000.0, 1.0f,  0.0f, 100.0f, 0.0f, 0.0f, 0.0f, true  },
        { "cinta drive 60",            48000.0, 1.0f, 60.0f, 100.0f, 0.0f, 0.0f, 0.0f, false },
        { "valvula drive 60",          48000.0, 2.0f, 60.0f, 100.0f, 0.0f, 0.0f, 0.0f, false },
        { "cinta mezcla 50",           48000.0, 1.0f, 60.0f,  50.0f, 0.0f, 0.0f, 0.0f, false },
        { "valvula mezcla 0",          48000.0, 2.0f, 60.0f,   0.0f, 0.0f, 0.0f, 0.0f, true  },
        { "4x limpio",                 48000.0, 0.0f,  0.0f, 100.0f, 0.0f, 1.0f, 0.0f, true  },
        { "4x cinta drive 60",         48000.0, 1.0f, 60.0f, 100.0f, 0.0f, 1.0f, 0.0f, false },
        { "4x valvula mezcla 40",      48000.0, 2.0f, 60.0f,  40.0f, 0.0f, 1.0f, 0.0f, false },
        { "fase natural",              48000.0, 0.0f,  0.0f, 100.0f, 1.0f, 0.0f, 0.0f, true  },
        { "fase lineal",               48000.0, 0.0f,  0.0f, 100.0f, 2.0f, 0.0f, 0.0f, true  },
        { "lineal + cinta",            48000.0, 1.0f, 60.0f, 100.0f, 2.0f, 0.0f, 0.0f, false },
        { "lineal + 4x valvula mix 30",48000.0, 2.0f, 60.0f,  30.0f, 2.0f, 1.0f, 0.0f, false },
        { "bypass minima",             48000.0, 1.0f, 60.0f, 100.0f, 0.0f, 0.0f, 1.0f, true  },
        { "bypass lineal",             48000.0, 1.0f, 60.0f, 100.0f, 2.0f, 0.0f, 1.0f, true  },
        { "lineal 44.1k",              44100.0, 0.0f,  0.0f, 100.0f, 2.0f, 0.0f, 0.0f, true  },
        { "lineal 96k",                96000.0, 0.0f,  0.0f, 100.0f, 2.0f, 0.0f, 0.0f, true  },
        { "lineal 192k",              192000.0, 0.0f,  0.0f, 100.0f, 2.0f, 0.0f, 0.0f, true  },
        { "4x + lineal 192k",         192000.0, 1.0f, 60.0f, 100.0f, 2.0f, 1.0f, 0.0f, false },
    };

    int failures = 0;
    for (const auto& c : cases)
    {
        MedidoresEQAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (c.sampleRate, blockSize);

        // EQ plano: sin pasos alto/bajo (las demás bandas con 0 dB no hacen nada)
        setParam (proc, "hp_on", 0.0f);
        setParam (proc, "lp_on", 0.0f);
        setParam (proc, "character", c.character);
        setParam (proc, "drive", c.drive);
        setParam (proc, "mix", c.mix);
        setParam (proc, "phase", c.phase);
        setParam (proc, "os", c.os);
        setParam (proc, "bypass", c.bypass);

        proc.prepareToPlay (c.sampleRate, blockSize);
        proc.rebuildKernels();
        proc.refreshLatency();
        const int reported = proc.getLatencySamples();

        // Los filtros FIR se cargan en un hilo de fondo: se deja correr silencio hasta que estén listos.
        for (int i = 0; i < 40; ++i)
        {
            juce::AudioBuffer<float> silence (2, blockSize);
            silence.clear();
            juce::MidiBuffer midi;
            proc.processBlock (silence, midi);
            juce::Thread::sleep (15);
        }

        std::vector<float> out;
        const int numBlocks = (reported + 4096) / blockSize + 3;
        for (int block = 0; block < numBlocks; ++block)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();
            if (block == 0)
                for (int ch = 0; ch < 2; ++ch) buffer.setSample (ch, impulseAt, 0.1f);

            juce::MidiBuffer midi;
            proc.processBlock (buffer, midi);
            for (int i = 0; i < blockSize; ++i) out.push_back (buffer.getSample (0, i));
        }

        int peak = 0;
        for (int i = 0; i < (int) out.size(); ++i)
            if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) peak])) peak = i;
        const int measured = peak - impulseAt;
        const float gain = std::abs (out[(size_t) peak]) / 0.1f;

        // Todos los filtros de la cadena son de fase lineal en la parte que retrasa (sobremuestreo y FIR): el pico de la respuesta
        // al impulso cae exactamente en la latencia informada. No se admite ninguna muestra de diferencia.
        const bool gainOk = ! c.checkGain || std::abs (gain - 1.0f) < 0.02f;
        const bool ok = measured == reported && gainOk;
        std::printf ("%-28s %6.0f Hz  informada = %5d  medida = %5d  ganancia = %.3f  %s\n",
                     c.name, c.sampleRate, reported, measured, gain, ok ? "OK" : "FALLO");
        if (! ok) ++failures;
    }

    std::printf ("%s\n", failures == 0 ? "Latencia coherente en todos los casos" : "HAY DESAJUSTES DE LATENCIA");
    return failures == 0 ? 0 : 1;
}
