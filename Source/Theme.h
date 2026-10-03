#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>

// Paleta de un tema visual. El plugin es una "placa frontal" de hardware: chapa con grano, serigrafía en tinta clara,
// capuchones de colores en los knobs y un visor oscuro para la curva.
struct Palette
{
    juce::String name;
    juce::Colour plateTop, plateBottom;   // chapa (degradado vertical)
    float grain;                          // intensidad del grano de la chapa
    juce::Colour ear;                     // orejas de rack y metal oscuro
    juce::Colour ink, inkMuted, line;     // serigrafía: texto, texto secundario y recuadros
    juce::Colour screen, trace;           // visor de la curva y su trazo
    juce::Colour inset, insetText;        // ventanas empotradas con valores
    juce::Colour control;                 // pulsadores y desplegables
    juce::Colour vuFace, vuInk, vuRed;    // medidores VU
    juce::Colour lampOff;                 // lámpara apagada
    juce::Colour accent;                  // resaltado (latón, rojo, ámbar...)
    std::array<juce::Colour, 8> band;     // capuchones de las 8 bandas
};

namespace Themes
{
    constexpr int count = 1;

    // Grafito y ámbar: estilo Studer
    inline const Palette& get (int = 0)
    {
        static const Palette grafito = {
            "Grafito",
            juce::Colour (0xff323235), juce::Colour (0xff222225), 0.06f,
            juce::Colour (0xff161619),
            juce::Colour (0xffe9e6dc), juce::Colour (0xffa19d90), juce::Colour (0xffe9e6dc),
            juce::Colour (0xff0a0a0c), juce::Colour (0xfff0a640),
            juce::Colour (0xff111113), juce::Colour (0xfff0a640),
            juce::Colour (0xff3b3b3f),
            juce::Colour (0xfff2dca3), juce::Colour (0xff2b2210), juce::Colour (0xffb53a24),
            juce::Colour (0xff29292c),
            juce::Colour (0xfff0a640),
            {{ juce::Colour (0xffe8694a), juce::Colour (0xfff0a640), juce::Colour (0xffd9d5c5), juce::Colour (0xff5fb3b3),
               juce::Colour (0xff9fcf6a), juce::Colour (0xffe08fb8), juce::Colour (0xffb58ad0), juce::Colour (0xff6f9bdb) }} };
        return grafito;
    }
}
