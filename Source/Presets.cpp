#include "Presets.h"
#include "PluginProcessor.h"

namespace
{
    using Values = std::vector<std::pair<const char*, float>>;
    struct Factory { const char* name; Values values; };

    // Los canales son índices: 0 = estéreo, 1 = Mid, 2 = Side, 3 = izquierdo, 4 = derecho. La fase: 0 mínima, 1 natural, 2 lineal. Las pendientes: 0 = 6, 1 = 12, 2 = 24, 3 = 48 dB/oct.
    const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> presets = {
            { "Flat", {} },
            { "Vocal: clean-up", {
                { "hp_freq", 90.0f }, { "hp_slope", 2.0f },
                { "b1_freq", 300.0f }, { "b1_gain", -3.0f }, { "b1_q", 1.2f },
                { "b3_freq", 3500.0f }, { "b3_gain", 2.5f }, { "b3_q", 0.9f },
                { "hs_freq", 10000.0f }, { "hs_gain", 2.0f } } },
            { "Kick", {
                { "hp_freq", 30.0f },
                { "ls_freq", 60.0f }, { "ls_gain", 3.0f },
                { "b1_freq", 350.0f }, { "b1_gain", -4.0f }, { "b1_q", 1.5f },
                { "b3_freq", 4000.0f }, { "b3_gain", 3.0f } } },
            { "Brightness", {
                { "hs_freq", 9000.0f }, { "hs_gain", 4.0f },
                { "b3_freq", 5000.0f }, { "b3_gain", 1.5f } } },
            { "Low cut", { { "hp_freq", 120.0f }, { "hp_slope", 3.0f } } },
            { "Telephone", {
                { "hp_freq", 400.0f }, { "hp_slope", 2.0f },
                { "lp_freq", 3400.0f }, { "lp_slope", 2.0f } } },
            { "Master: more air (Side)", {
                { "hs_freq", 8000.0f }, { "hs_gain", 3.0f }, { "hs_ch", 2.0f },
                { "ls_freq", 150.0f }, { "ls_gain", -2.0f }, { "ls_ch", 2.0f } } },
            { "Vocal: dynamic de-esser", {
                { "hp_freq", 90.0f }, { "hp_slope", 2.0f },
                { "b3_freq", 6500.0f }, { "b3_gain", -8.0f }, { "b3_q", 3.0f },
                { "b3_dyn", 1.0f }, { "b3_thr", -32.0f }, { "b3_ratio", 4.0f },
                { "b3_attack", 2.0f }, { "b3_release", 60.0f } } },
            { "Bass: tight lows", {
                { "hp_freq", 35.0f }, { "hp_slope", 2.0f },
                { "ls_freq", 90.0f }, { "ls_gain", -6.0f }, { "ls_type", 1.0f }, { "ls_q", 1.2f },
                { "ls_dyn", 1.0f }, { "ls_thr", -22.0f }, { "ls_ratio", 3.0f },
                { "ls_attack", 25.0f }, { "ls_release", 250.0f } } },
            { "Master: presence (Mid)", {
                { "b3_freq", 2500.0f }, { "b3_gain", 2.0f }, { "b3_q", 0.8f }, { "b3_ch", 1.0f } } },
            { "Master: gentle linear phase", {
                { "phase", 2.0f }, { "hp_freq", 25.0f }, { "hp_slope", 2.0f },
                { "ls_freq", 90.0f }, { "ls_gain", 1.0f }, { "ls_q", 0.6f },
                { "b3_freq", 3000.0f }, { "b3_gain", -0.8f }, { "b3_q", 0.7f },
                { "hs_freq", 12000.0f }, { "hs_gain", 1.2f }, { "hs_q", 0.6f },
                { "mono_freq", 100.0f }, { "character", 0.0f } } },
            { "Master: Pultec warmth", {
                { "phase", 1.0f },
                { "ls_type", 2.0f }, { "ls_freq", 60.0f }, { "ls_gain", 3.0f }, { "ls_cut", 2.5f }, { "ls_q", 0.7f },
                { "hs_type", 4.0f }, { "hs_freq", 8000.0f }, { "hs_gain", 1.5f } } },
            { "Master: tilt", {
                { "phase", 2.0f }, { "ls_type", 3.0f }, { "ls_freq", 1000.0f }, { "ls_gain", 1.5f }, { "ls_q", 0.5f } } },
            { "Master: mono bass", {
                { "mono_freq", 120.0f }, { "hp_freq", 25.0f }, { "hp_slope", 2.0f } } },
        };
        return presets;
    }
}

juce::File PresetManager::folder()
{
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    return base.getChildFile ("eCU-10 MST").getChildFile ("Presets");
}

// El nombre puede llevar carpetas ("Master/Suave"): cada trozo se limpia por separado.
juce::File PresetManager::fileFor (const juce::String& name)
{
    juce::File file = folder();
    const auto parts = juce::StringArray::fromTokens (name, "/", "");
    for (int i = 0; i < parts.size(); ++i)
    {
        const auto part = juce::File::createLegalFileName (parts[i].trim());
        if (part.isEmpty()) continue;
        file = file.getChildFile (i == parts.size() - 1 ? part + ".xml" : part);
    }
    return file;
}

juce::StringArray PresetManager::factoryNames() const
{
    juce::StringArray names;
    for (auto& p : factory()) names.add (juce::String::fromUTF8 (p.name));
    return names;
}

juce::StringArray PresetManager::userNames() const
{
    juce::StringArray names;
    const auto root = folder();
    for (auto& f : root.findChildFiles (juce::File::findFiles, true, "*.xml"))
        names.add (f.getRelativePathFrom (root).replaceCharacter ('\\', '/').upToLastOccurrenceOf (".xml", false, false));
    names.sort (true);
    return names;
}

void PresetManager::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void PresetManager::resetToDefaults()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! EQ::isViewParam (rp->paramID))   // la vista (analizador, rango) no cambia con los presets
                p->setValueNotifyingHost (p->getDefaultValue());
}

void PresetManager::loadFactory (const juce::String& name)
{
    for (auto& p : factory())
        if (name == juce::String::fromUTF8 (p.name))
        {
            resetToDefaults();
            for (auto& v : p.values) setParam (v.first, v.second);
            return;
        }
}

bool PresetManager::loadUser (const juce::String& name)
{
    if (auto xml = juce::parseXML (fileFor (name)))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            return true;
        }
    return false;
}

bool PresetManager::saveUser (const juce::String& name)
{
    if (name.trim().isEmpty()) return false;
    const auto file = fileFor (name.trim());
    if (! file.getParentDirectory().createDirectory()) return false;
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (file);
    return false;
}

bool PresetManager::removeUser (const juce::String& name)
{
    return fileFor (name).deleteFile();
}

bool PresetManager::exportTo (const juce::File& file) const
{
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (file.hasFileExtension ("xml") ? file : file.withFileExtension ("xml"));
    return false;
}

juce::String PresetManager::importFrom (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto name = file.getFileNameWithoutExtension();
            if (saveUser (name) && xml->writeTo (fileFor (name))) return name;
        }
    return {};
}
