#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Presets de fábrica (definidos en código) y de usuario (archivos XML en
// ~/Library/Application Support/eCU-10 MST/Presets (con subcarpetas: el nombre "Carpeta/Preset")).
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

    static juce::File folder();

    juce::StringArray factoryNames() const;
    juce::StringArray userNames() const;

    void loadFactory (const juce::String& name);
    bool loadUser (const juce::String& name);
    bool saveUser (const juce::String& name);
    bool removeUser (const juce::String& name);
    bool exportTo (const juce::File& file) const;      // guarda los ajustes actuales en un archivo
    juce::String importFrom (const juce::File& file);  // copia un archivo de preset a la carpeta de usuario; devuelve su nombre ('' si falla)

private:
    static juce::File fileFor (const juce::String& name);
    void resetToDefaults();
    void setParam (const juce::String& id, float value);

    juce::AudioProcessorValueTreeState& apvts;
};
