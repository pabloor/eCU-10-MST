#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// Ecualizador de mastering de 8 bandas: paso alto, shelf de graves, cuatro medias, shelf de agudos y paso bajo.
//  - Pasos alto/bajo con pendiente ajustable (6/12/24/48 dB/oct).
//  - Cada banda (salvo los pasos) actúa sobre el estéreo, solo el Mid, solo el Side, solo el canal izquierdo o solo el derecho.
//  - Tipos: las medias son campana o notch; los shelves, shelf, campana, Pultec (realce y atenuación a la vez) o tilt.
//  - Las bandas que no son de corte pueden ser dinámicas (campana y shelf).
//  - Estilo de curva (Moderna, Clásica, Americana, Vintage): cómo cambia la Q con la ganancia.
//  - Todo el cálculo de filtros es en doble precisión.
namespace EQ
{
    enum Band { HighPass, LowShelf, Bell1, Bell2, Bell3, Bell4, HighShelf, LowPass, NumBands };
    constexpr int MaxStages = 4;   // 48 dB/oct = 4 biquads

    inline bool isCut (int b)   { return b == HighPass || b == LowPass; }
    inline bool isShelf (int b) { return b == LowShelf || b == HighShelf; }
    inline bool hasType (int b) { return ! isCut (b); }
    inline bool hasDyn (int b)  { return ! isCut (b); }

    // JUCE interpreta los textos entre comillas (const char*) como ASCII: todo texto con tildes, ñ, ¿ o … debe pasar por aquí.
    inline juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    struct BandInfo { const char* id; const char* name; float freq; float gain; float q; };

    inline const BandInfo bands[NumBands] = {
        { "hp", "Paso alto",  20.0f,    0.0f, 0.707f },
        { "ls", "Graves",     100.0f,   0.0f, 0.707f },
        { "b1", "Medio 1",    250.0f,   0.0f, 1.0f },
        { "b2", "Medio 2",    800.0f,   0.0f, 1.0f },
        { "b3", "Medio 3",    2500.0f,  0.0f, 1.0f },
        { "b4", "Medio 4",    5500.0f,  0.0f, 1.0f },
        { "hs", "Agudos",     10000.0f, 0.0f, 0.707f },
        { "lp", "Paso bajo",  20000.0f, 0.0f, 0.707f },
    };

    inline juce::String freqId  (int b) { return juce::String (bands[b].id) + "_freq"; }
    inline juce::String gainId  (int b) { return juce::String (bands[b].id) + "_gain"; }
    inline juce::String qId     (int b) { return juce::String (bands[b].id) + "_q"; }
    inline juce::String onId    (int b) { return juce::String (bands[b].id) + "_on"; }
    inline juce::String slopeId (int b) { return juce::String (bands[b].id) + "_slope"; }
    inline juce::String chId    (int b) { return juce::String (bands[b].id) + "_ch"; }
    inline juce::String typeId  (int b) { return juce::String (bands[b].id) + "_type"; }
    inline juce::String cutId   (int b) { return juce::String (bands[b].id) + "_cut"; }   // atenuación del modo Pultec (solo shelves)
    inline juce::String dynId   (int b) { return juce::String (bands[b].id) + "_dyn"; }
    inline juce::String thrId   (int b) { return juce::String (bands[b].id) + "_thr"; }
    inline juce::String ratioId (int b) { return juce::String (bands[b].id) + "_ratio"; }
    inline juce::String attackId (int b)  { return juce::String (bands[b].id) + "_attack"; }
    inline juce::String releaseId (int b) { return juce::String (bands[b].id) + "_release"; }
    inline juce::String dmodeId (int b)   { return juce::String (bands[b].id) + "_dmode"; }   // 0 compresión, 1 expansión

    inline const char* inId = "in_gain";
    inline const char* outId = "out_gain";
    inline const char* driveId = "drive";
    inline const char* mixId = "mix";   // mezcla seco/saturado (saturación en paralelo)
    inline const char* characterId = "character";
    inline const char* styleId = "style";
    inline const char* phaseId = "phase";         // 0 fase mínima, 1 fase natural, 2 fase lineal
    inline const char* osId = "os";               // sobremuestreo de la saturación: 0 = 2x, 1 = 4x
    inline const char* detId = "dyn_det";         // detector de la dinámica: 0 pico, 1 RMS
    inline const char* gainRangeId = "gain_range";   // rango de los knobs de ganancia (solo vista)
    inline const char* bypassId = "bypass";
    inline const char* autoGainId = "autogain";   // compensa el nivel (en loudness) para comparar con el original
    inline const char* monoFreqId = "mono_freq";  // graves en mono por debajo de esta frecuencia (0 = apagado)
    inline const char* widthId = "width";         // anchura estéreo (Side)
    inline const char* ditherId = "dither";       // 0 apagado, 1 = 16 bits, 2 = 24 bits
    inline const char* scId = "dyn_sc";           // detector de la dinámica: 0 interno, 1 sidechain externo
    inline const char* soloId = "solo";           // 0 = ninguna; 1..8 = banda en solo
    inline const char* analyzerId = "an_mode";    // 0 apagado, 1 post-EQ, 2 pre-EQ
    inline const char* analyzerSpeedId = "an_speed";
    inline const char* analyzerResId = "an_res";
    inline const char* analyzerSmoothId = "an_smooth";
    inline const char* analyzerHoldId = "an_hold";
    inline const char* rangeId = "view_range";    // rango vertical de la curva: ±6, ±12, ±24 dB

    inline juce::StringArray slopeNames()     { return { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" }; }
    inline juce::StringArray placementNames() { return { utf8 ("Estéreo"), "Mid", "Side", "Izquierdo", "Derecho" }; }
    inline juce::StringArray characterNames() { return { "Limpio", "Cinta", utf8 ("Válvula") }; }
    inline juce::StringArray styleNames()     { return { "Moderna", utf8 ("Clásica"), "Americana", "Vintage" }; }
    inline juce::StringArray phaseNames()     { return { utf8 ("Mínima"), "Natural", "Lineal" }; }
    inline juce::StringArray osNames()        { return { "2x", "4x" }; }
    inline juce::StringArray detNames()       { return { "Pico", "RMS" }; }
    inline juce::StringArray dmodeNames()     { return { utf8 ("Compresión"), utf8 ("Expansión") }; }
    inline juce::StringArray gainRangeNames() { return { utf8 ("\u00b118 dB"), utf8 ("\u00b112 dB"), utf8 ("\u00b16 dB"), utf8 ("\u00b13 dB") }; }
    inline float gainRangeFor (int index)     { return index == 1 ? 12.0f : (index == 2 ? 6.0f : (index == 3 ? 3.0f : 18.0f)); }
    inline juce::StringArray ditherNames()    { return { "Apagado", "16 bits", "24 bits" }; }
    inline juce::StringArray scNames()        { return { "Interno", "Externo" }; }
    inline juce::StringArray analyzerNames()  { return { "Apagado", "Post-EQ", "Pre-EQ" }; }
    inline juce::StringArray speedNames()     { return { "Lenta", "Media", utf8 ("Rápida") }; }
    inline juce::StringArray analyzerResNames()    { return { "Normal", "Fina", utf8 ("Máxima") }; }
    inline juce::StringArray analyzerSmoothNames() { return { "Sin suavizar", "1/6 oct", "1/3 oct" }; }
    inline juce::StringArray holdNames()      { return { "Sin pico", "Con pico" }; }
    inline juce::StringArray rangeNames()     { return { utf8 ("±6 dB"), utf8 ("±12 dB"), utf8 ("±24 dB") }; }
    inline float rangeDbFor (int index)       { return index == 0 ? 6.0f : (index == 2 ? 24.0f : 12.0f); }
    inline int analyzerOrderFor (int index)   { return index == 0 ? 11 : (index == 2 ? 15 : 13); }

    inline juce::StringArray typeNames (int b)
    {
        if (isShelf (b)) return { "Shelf", "Campana", "Pultec", "Tilt", "Baxandall" };
        return { "Campana", "Notch" };
    }
    inline juce::StringArray soloNames()
    {
        juce::StringArray n { "Ninguna" };
        for (int b = 0; b < NumBands; ++b) n.add (bands[b].name);
        return n;
    }

    // Ajustes de vista y de monitorización: no se automatizan ni cambian al cargar un preset.
    inline bool isViewParam (const juce::String& id)
    {
        return id == analyzerId || id == analyzerSpeedId || id == analyzerResId || id == analyzerSmoothId || id == analyzerHoldId
               || id == rangeId || id == gainRangeId || id == soloId || id == bypassId || id == autoGainId;
    }

    inline const juce::Colour bandColours[NumBands] = {
        juce::Colour (0xffd9603f), juce::Colour (0xffe8a23c), juce::Colour (0xffcfc9b0), juce::Colour (0xff4fa6a8),
        juce::Colour (0xff9db55e), juce::Colour (0xffd88fb5), juce::Colour (0xffb07fc4), juce::Colour (0xff6f9bdb) };

    //==========================================================================
    // Biquad en doble precisión (coeficientes ya normalizados).
    struct Bq
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        double magnitude (double freq, double sampleRate) const
        {
            const double w = juce::MathConstants<double>::twoPi * freq / sampleRate;
            const double c1 = std::cos (w), c2 = std::cos (2.0 * w);
            const double num = b0 * b0 + b1 * b1 + b2 * b2 + 2.0 * (b0 * b1 + b1 * b2) * c1 + 2.0 * b0 * b2 * c2;
            const double den = 1.0 + a1 * a1 + a2 * a2 + 2.0 * (a1 + a1 * a2) * c1 + 2.0 * a2 * c2;
            return std::sqrt (juce::jmax (num, 0.0) / juce::jmax (den, 1.0e-30));
        }

        bool isNeutral() const
        {
            return std::abs (b0 - 1.0) + std::abs (b1) + std::abs (b2) + std::abs (a1) + std::abs (a2) < 1.0e-9;
        }
    };

    inline Bq normalised (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        Bq c;
        const double inv = 1.0 / a0;
        c.b0 = b0 * inv; c.b1 = b1 * inv; c.b2 = b2 * inv; c.a1 = a1 * inv; c.a2 = a2 * inv;
        return c;
    }

    // Fórmulas RBJ del "Audio EQ Cookbook".
    struct Trig { double cs, sn, alpha; };
    inline Trig trig (double sr, double f, double q)
    {
        const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (2.0, sr * 0.49, f) / sr;
        const double sn = std::sin (w0);
        return { std::cos (w0), sn, sn / (2.0 * juce::jmax (q, 0.01)) };
    }

    inline Bq rbjPeak (double sr, double f, double q, double gainDb)
    {
        const auto t = trig (sr, f, q);
        const double A = std::pow (10.0, gainDb / 40.0);
        return normalised (1.0 + t.alpha * A, -2.0 * t.cs, 1.0 - t.alpha * A, 1.0 + t.alpha / A, -2.0 * t.cs, 1.0 - t.alpha / A);
    }

    inline Bq rbjShelf (bool low, double sr, double f, double q, double gainDb)
    {
        const auto t = trig (sr, f, q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double am1 = A - 1.0, ap1 = A + 1.0, beta = 2.0 * std::sqrt (A) * t.alpha, am1c = am1 * t.cs;
        if (low)
            return normalised (A * (ap1 - am1c + beta), A * 2.0 * (am1 - ap1 * t.cs), A * (ap1 - am1c - beta),
                               ap1 + am1c + beta, -2.0 * (am1 + ap1 * t.cs), ap1 + am1c - beta);
        return normalised (A * (ap1 + am1c + beta), A * -2.0 * (am1 + ap1 * t.cs), A * (ap1 + am1c - beta),
                           ap1 - am1c + beta, 2.0 * (am1 - ap1 * t.cs), ap1 - am1c - beta);
    }

    inline Bq rbjNotch (double sr, double f, double q)
    {
        const auto t = trig (sr, f, q);
        return normalised (1.0, -2.0 * t.cs, 1.0, 1.0 + t.alpha, -2.0 * t.cs, 1.0 - t.alpha);
    }
    inline Bq rbjBandPass (double sr, double f, double q)
    {
        const auto t = trig (sr, f, q);
        return normalised (t.alpha, 0.0, -t.alpha, 1.0 + t.alpha, -2.0 * t.cs, 1.0 - t.alpha);
    }
    inline Bq rbjLowPass (double sr, double f, double q)
    {
        const auto t = trig (sr, f, q);
        return normalised ((1.0 - t.cs) * 0.5, 1.0 - t.cs, (1.0 - t.cs) * 0.5, 1.0 + t.alpha, -2.0 * t.cs, 1.0 - t.alpha);
    }
    inline Bq rbjHighPass (double sr, double f, double q)
    {
        const auto t = trig (sr, f, q);
        return normalised ((1.0 + t.cs) * 0.5, -(1.0 + t.cs), (1.0 + t.cs) * 0.5, 1.0 + t.alpha, -2.0 * t.cs, 1.0 - t.alpha);
    }
    // Primer orden (6 dB/oct) escrito como biquad.
    inline Bq firstOrder (bool highPass, double sr, double f)
    {
        const double k = std::tan (juce::MathConstants<double>::pi * juce::jlimit (2.0, sr * 0.49, f) / sr);
        return highPass ? normalised (1.0, -1.0, 0.0, 1.0 + k, k - 1.0, 0.0)
                        : normalised (k, k, 0.0, 1.0 + k, k - 1.0, 0.0);
    }

    //==========================================================================
    enum Shape { Peak, LowShelfShape, HighShelfShape, NotchShape, PultecLow, PultecHigh, TiltLow, TiltHigh, BaxLow, BaxHigh };

    inline bool dynEligible (Shape s) { return s == Peak || s == LowShelfShape || s == HighShelfShape; }

    // Estilo de curva: cómo cambia la Q con la ganancia (solo campanas; el Vintage además da resonancia a los shelves).
    //   0 Moderna:   Q constante.
    //   1 Clásica:   la campana se ensancha al subir la ganancia y se estrecha al bajarla.
    //   2 Americana: al revés: se estrecha al subir y se ensancha al bajar (Q proporcional).
    //   3 Vintage:   como la Clásica, y los shelves con un pequeño rebote antes de la curva.
    inline float styleQ (int style, Shape shape, float q, float gainDb)
    {
        if (shape == Peak)
        {
            if (style == 1 || style == 3) q *= std::exp (-0.066f * gainDb);
            else if (style == 2)          q *= std::exp ( 0.066f * gainDb);
        }
        else if ((shape == LowShelfShape || shape == HighShelfShape) && style == 3)
            q *= 1.3f;
        return juce::jlimit (0.05f, 30.0f, q);
    }

    // Parámetros actuales de una banda que no es de corte.
    struct Settings
    {
        Shape shape = Peak;
        float freq = 1000.0f, gainDb = 0.0f, q = 1.0f, cutDb = 0.0f;
        int style = 1;
    };

    inline Settings readSettings (int b, const juce::AudioProcessorValueTreeState& apvts, double sampleRate)
    {
        auto read = [&] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
        Settings s;
        s.freq = juce::jmin (read (freqId (b)), (float) (sampleRate * 0.49));
        s.gainDb = read (gainId (b));
        s.q = read (qId (b));
        s.style = (int) read (styleId);

        const int type = juce::jmax (0, (int) read (typeId (b)));
        if (b == LowShelf)
        {
            s.shape = type == 1 ? Peak : type == 2 ? PultecLow : type == 3 ? TiltLow : type == 4 ? BaxLow : LowShelfShape;
            s.cutDb = read (cutId (b));
        }
        else if (b == HighShelf)
        {
            s.shape = type == 1 ? Peak : type == 2 ? PultecHigh : type == 3 ? TiltHigh : type == 4 ? BaxHigh : HighShelfShape;
            s.cutDb = read (cutId (b));
        }
        else
            s.shape = type == 1 ? NotchShape : Peak;
        return s;
    }

    //==========================================================================
    // Filtros (en cascada) de una banda: un biquad, dos (Pultec, tilt) o hasta cuatro (pasos alto/bajo de 48 dB/oct).
    struct Design
    {
        Bq st[MaxStages];
        int n = 1;

        double magnitude (double freq, double sampleRate) const
        {
            double m = 1.0;
            for (int i = 0; i < n; ++i) m *= st[i].magnitude (freq, sampleRate);
            return m;
        }
    };
    using BandFilter = Design;

    inline Design makeDesign (const Settings& s, float gainDb, double sr)
    {
        Design d;
        const double f = s.freq;
        switch (s.shape)
        {
            case Peak:           d.st[0] = rbjPeak (sr, f, styleQ (s.style, s.shape, s.q, gainDb), gainDb); break;
            case LowShelfShape:  d.st[0] = rbjShelf (true,  sr, f, styleQ (s.style, s.shape, s.q, gainDb), gainDb); break;
            case HighShelfShape: d.st[0] = rbjShelf (false, sr, f, styleQ (s.style, s.shape, s.q, gainDb), gainDb); break;
            case NotchShape:     d.st[0] = rbjNotch (sr, f, juce::jlimit (0.05f, 30.0f, s.q)); break;
            case PultecLow:
                // Realce de graves y atenuación en la misma frecuencia (el truco de los Pultec): el realce es más abrupto que la atenuación.
                d.n = 2;
                d.st[0] = rbjShelf (true, sr, f, juce::jlimit (0.05f, 30.0f, s.q), gainDb);
                d.st[1] = rbjShelf (true, sr, f, 0.35, -s.cutDb);
                break;
            case PultecHigh:
                // Realce en campana y atenuación en shelf de agudos.
                d.n = 2;
                d.st[0] = rbjPeak (sr, f, juce::jlimit (0.05f, 30.0f, s.q), gainDb);
                d.st[1] = rbjShelf (false, sr, f, 0.45, -s.cutDb);
                break;
            case BaxLow:   d.st[0] = rbjShelf (true,  sr, f, 0.4, gainDb); break;   // shelf suave de 6 dB/oct, como un control de tono Baxandall
            case BaxHigh:  d.st[0] = rbjShelf (false, sr, f, 0.4, gainDb); break;
            case TiltLow:
            case TiltHigh:
            {
                // Inclinación espectral alrededor de la frecuencia: lo que sube un lado lo baja el otro.
                const float half = 0.5f * gainDb * (s.shape == TiltLow ? 1.0f : -1.0f);
                const float q = juce::jlimit (0.05f, 30.0f, s.q);
                d.n = 2;
                d.st[0] = rbjShelf (true,  sr, f, q,  half);
                d.st[1] = rbjShelf (false, sr, f, q, -half);
                break;
            }
        }
        return d;
    }

    // Filtro de una banda a partir de los parámetros actuales (también lo usa el editor para dibujar la curva).
    inline Design makeBand (int b, const juce::AudioProcessorValueTreeState& apvts, double sampleRate)
    {
        Design bf;

        // Banda desactivada: filtro neutro.
        if (apvts.getRawParameterValue (onId (b))->load() < 0.5f)
            return bf;

        if (isCut (b))
        {
            const float f = juce::jmin (apvts.getRawParameterValue (freqId (b))->load(), (float) (sampleRate * 0.49));
            const bool hp = (b == HighPass);
            const int slope = juce::jlimit (0, 3, (int) apvts.getRawParameterValue (slopeId (b))->load());

            if (slope == 0)
            {
                bf.st[0] = firstOrder (hp, sampleRate, f);
            }
            else
            {
                // Butterworth de orden 2, 4 u 8: una Q distinta por cada biquad de la cascada.
                const int order = 1 << slope;
                bf.n = order / 2;
                for (int k = 1; k <= bf.n; ++k)
                {
                    const double q = 1.0 / (2.0 * std::sin ((2.0 * (double) k - 1.0) * juce::MathConstants<double>::pi / (2.0 * (double) order)));
                    bf.st[k - 1] = hp ? rbjHighPass (sampleRate, f, q) : rbjLowPass (sampleRate, f, q);
                }
            }
            return bf;
        }

        const auto s = readSettings (b, apvts, sampleRate);
        return makeDesign (s, s.gainDb, sampleRate);
    }

    // Dónde actúa la banda: 0 = estéreo (L/R), 1 = Mid, 2 = Side, 3 = solo izquierdo, 4 = solo derecho.
    // Los filtros paso alto/bajo siempre actúan en estéreo: en Mid/Side desfasarían los graves entre Mid y Side.
    inline int placement (int b, const juce::AudioProcessorValueTreeState& apvts)
    {
        if (isCut (b)) return 0;
        return juce::jlimit (0, 4, (int) apvts.getRawParameterValue (chId (b))->load());
    }
}
