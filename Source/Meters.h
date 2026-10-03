#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// Utilidades de audio en doble precisión: líneas de retardo y medidores (loudness, true peak, correlación).

// Retardo entero para una señal (un canal).
struct DelayLine
{
    std::vector<double> buf;
    int size = 1, pos = 0;

    void prepare (int maxDelay) { size = juce::jmax (1, maxDelay + 1); buf.assign ((size_t) size, 0.0); pos = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }

    inline double process (double x, int delay)
    {
        buf[(size_t) pos] = x;
        int r = pos - juce::jlimit (0, size - 1, delay);
        if (r < 0) r += size;
        const double y = buf[(size_t) r];
        if (++pos >= size) pos = 0;
        return y;
    }
};

//==============================================================================
// Loudness según ITU-R BS.1770 / EBU R128: filtro K, momentáneo (400 ms), corto plazo (3 s) e integrado con puertas.
class LoudnessMeter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // Etapa 1: shelf de agudos (+4 dB); etapa 2: paso alto de 38 Hz.
        for (int ch = 0; ch < 2; ++ch)
        {
            {
                const double G = 3.99984385397, Q = 0.7071752369554193, fc = 1681.9744509555319;
                const double K = std::tan (juce::MathConstants<double>::pi * fc / sr);
                const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                shelf[ch] = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                              2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0, 0.0, 0.0 };
            }
            {
                const double Q = 0.5003270373253953, fc = 38.13547087613982;
                const double K = std::tan (juce::MathConstants<double>::pi * fc / sr);
                const double a0 = 1.0 + K / Q + K * K;
                hp[ch] = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0, 0.0, 0.0 };
            }
        }
        blockLen = juce::jmax (1, juce::roundToInt (sr * 0.1));
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch) { shelf[ch].z1 = shelf[ch].z2 = hp[ch].z1 = hp[ch].z2 = 0.0; }
        sum = 0.0; count = 0; filled = 0; idx = 0;
        blocks.fill (0.0);
        histogram.fill (0);
        sinceIntegrated = 0;
        momentaryLufs = shortTermLufs = integratedLufs = -200.0f;
        publish();
    }

    // Se llama desde el hilo de audio.
    void process (const double* l, const double* r, int n)
    {
        if (resetRequested.exchange (false)) reset();

        for (int i = 0; i < n; ++i)
        {
            const double a = hp[0].tick (shelf[0].tick (l[i]));
            const double b = hp[1].tick (shelf[1].tick (r[i]));
            sum += a * a + b * b;
            if (++count >= blockLen)
            {
                pushBlock (sum / (double) count);
                sum = 0.0; count = 0;
            }
        }
    }

    void requestReset() { resetRequested.store (true); }
    float momentary() const  { return momentaryAtomic.load(); }
    float shortTerm() const  { return shortTermAtomic.load(); }
    float integrated() const { return integratedAtomic.load(); }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline double tick (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static double toLufs (double energy) { return energy > 1.0e-12 ? -0.691 + 10.0 * std::log10 (energy) : -200.0; }

    static constexpr int numBins = 800;   // de -70 a +10 LUFS en pasos de 0,1

    void pushBlock (double energy)
    {
        blocks[(size_t) idx] = energy;
        idx = (idx + 1) % (int) blocks.size();
        filled = juce::jmin (filled + 1, (int) blocks.size());

        auto average = [&] (int num)
        {
            double s = 0.0;
            for (int k = 0; k < num; ++k) s += blocks[(size_t) ((idx - 1 - k + 2 * (int) blocks.size()) % (int) blocks.size())];
            return s / (double) num;
        };

        if (filled >= 4)
        {
            momentaryLufs = (float) toLufs (average (4));
            const float l = momentaryLufs;
            if (l > -70.0f)
                ++histogram[(size_t) juce::jlimit (0, numBins - 1, (int) ((l + 70.0f) * 10.0f))];
        }
        if (filled >= 30) shortTermLufs = (float) toLufs (average (30));

        if (++sinceIntegrated >= 10) { sinceIntegrated = 0; computeIntegrated(); }
        publish();
    }

    void computeIntegrated()
    {
        double total = 0.0; juce::uint64 num = 0;
        auto binEnergy = [] (int bin) { return std::pow (10.0, ((double) bin / 10.0 - 70.0 + 0.691) / 10.0); };
        for (int i = 0; i < numBins; ++i) { total += (double) histogram[(size_t) i] * binEnergy (i); num += histogram[(size_t) i]; }
        if (num == 0) { integratedLufs = -200.0f; return; }

        const double gate = toLufs (total / (double) num) - 10.0;   // puerta relativa
        double sumGated = 0.0; juce::uint64 numGated = 0;
        for (int i = 0; i < numBins; ++i)
            if ((double) i / 10.0 - 70.0 >= gate) { sumGated += (double) histogram[(size_t) i] * binEnergy (i); numGated += histogram[(size_t) i]; }
        integratedLufs = numGated > 0 ? (float) toLufs (sumGated / (double) numGated) : -200.0f;
    }

    void publish()
    {
        momentaryAtomic.store (momentaryLufs);
        shortTermAtomic.store (shortTermLufs);
        integratedAtomic.store (integratedLufs);
    }

    double sr = 48000.0;
    Biquad shelf[2], hp[2];
    int blockLen = 4800, count = 0, filled = 0, idx = 0, sinceIntegrated = 0;
    double sum = 0.0;
    std::array<double, 30> blocks {};                 // energía de bloques de 100 ms
    std::array<juce::uint32, numBins> histogram {};   // ventanas de 400 ms por nivel, para el integrado
    float momentaryLufs = -200.0f, shortTermLufs = -200.0f, integratedLufs = -200.0f;
    std::atomic<float> momentaryAtomic { -200.0f }, shortTermAtomic { -200.0f }, integratedAtomic { -200.0f };
    std::atomic<bool> resetRequested { false };
};

//==============================================================================
// True peak: pico de la señal sobremuestreada x4 (interpolación sinc con ventana, 16 puntos).
class TruePeakMeter
{
public:
    TruePeakMeter()
    {
        for (int p = 1; p <= 3; ++p)
            for (int i = 0; i < 16; ++i)
            {
                const double d = (double) (i - 7) - (double) p / 4.0;
                const double sinc = std::abs (d) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * d) / (juce::MathConstants<double>::pi * d);
                const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * d / 8.0);
                table[(size_t) (p - 1)][(size_t) i] = sinc * w;
            }
    }

    void reset() { for (auto& h : hist) h.fill (0.0); pos = 0; peak = 0.0; peakAtomic.store (0.0f); }

    void process (const double* l, const double* r, int n)
    {
        if (resetRequested.exchange (false)) reset();
        const double* in[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& h = hist[(size_t) ch];
                h[(size_t) pos] = in[ch][i];
                peak = juce::jmax (peak, std::abs (in[ch][i]));
            }
            pos = (pos + 1) & 15;   // pos apunta ahora a la muestra más antigua
            for (int ch = 0; ch < 2; ++ch)
                for (int p = 0; p < 3; ++p)
                {
                    double y = 0.0;
                    for (int k = 0; k < 16; ++k) y += hist[(size_t) ch][(size_t) ((pos + k) & 15)] * table[(size_t) p][(size_t) k];
                    peak = juce::jmax (peak, std::abs (y));
                }
        }
        peakAtomic.store ((float) peak);
    }

    void requestReset() { resetRequested.store (true); }
    float peakLinear() const { return peakAtomic.load(); }

private:
    std::array<std::array<double, 16>, 3> table {};
    std::array<double, 16> hist[2] {};
    int pos = 0;
    double peak = 0.0;
    std::atomic<float> peakAtomic { 0.0f };
    std::atomic<bool> resetRequested { false };
};
