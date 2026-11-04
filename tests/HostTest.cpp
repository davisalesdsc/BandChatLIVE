// Host de teste: carrega o VST3 compilado como um DAW carregaria e confere
// o básico que todo host exige (estilo pluginval), mais o fluxo completo:
//
//   HostTest <plugin.vst3> [fala.wav] [captura.png]
//
// Com TAV_DEMO=1 no ambiente, o WAV é tocado em tempo real pelo plugin e o
// teste confere as frases no histórico diário que o plugin grava.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok     " : "  FALHOU ") << what.toRawUTF8() << std::endl;
    if (! ok) ++failures;
}

static std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& fm, const juce::String& path,
                                                        double rate, int block)
{
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, path);
    if (found.isEmpty()) return {};
    juce::String err;
    return fm.createPluginInstance (*found[0], rate, block, err);
}

static void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

static juce::AudioBuffer<float> readWav48kStereo (const juce::File& f)
{
    juce::AudioFormatManager afm;
    afm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (afm.createReaderFor (f));
    if (r == nullptr) return {};
    juce::AudioBuffer<float> src ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&src, 0, (int) r->lengthInSamples, 0, true, true);
    const double ratio = r->sampleRate / 48000.0;
    const int n = (int) (src.getNumSamples() / ratio);
    juce::AudioBuffer<float> out (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double pos = i * ratio;
        const int a = juce::jmin ((int) pos, src.getNumSamples() - 2);
        const float fr = (float) (pos - a);
        const float v = src.getSample (0, a) + (src.getSample (0, a + 1) - src.getSample (0, a)) * fr;
        out.setSample (0, i, v);
        out.setSample (1, i, v);
    }
    return out;
}

int main (int argc, char* argv[])
{
    if (argc < 2) { std::cerr << "uso: HostTest plugin.vst3 [fala.wav] [captura.png]\n"; return 2; }
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    const juce::String path = juce::String::fromUTF8 (argv[1]);

    std::cout << "== carregar / formatos\n";
    auto p = load (fm, path, 48000.0, 512);
    check (p != nullptr, "plugin carrega");
    if (p == nullptr) return 1;
    check (p->getName().isNotEmpty(), "nome: " + p->getName());
    check (p->getParameters().size() >= 5, "parâmetros expostos: " + juce::String (p->getParameters().size()));

    std::cout << "== processamento em várias taxas e blocos (áudio passa intacto)\n";
    juce::Random rnd (42);
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 32, 333, 512, 2048 })
        {
            p->setPlayConfigDetails (2, 2, rate, block);
            p->prepareToPlay (rate, block);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            bool intact = true, finite = true;
            for (int k = 0; k < 20; ++k)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i) buf.setSample (ch, i, rnd.nextFloat() * 2.0f - 1.0f);
                juce::AudioBuffer<float> copy (buf);
                p->processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                    {
                        if (buf.getSample (ch, i) != copy.getSample (ch, i)) intact = false;
                        if (! std::isfinite (buf.getSample (ch, i))) finite = false;
                    }
            }
            // bloco MENOR que o anunciado (comum no fim de loop / automação)
            juce::AudioBuffer<float> small (2, juce::jmax (1, block / 3));
            small.clear();
            p->processBlock (small, midi);
            p->releaseResources();
            check (intact && finite, juce::String (rate, 0) + " Hz / bloco " + juce::String (block));
        }

    std::cout << "== parâmetros e estado\n";
    p->prepareToPlay (48000.0, 512);
    auto& prms = p->getParameters();
    for (auto* prm : prms) prm->setValueNotifyingHost (0.3f);
    juce::MemoryBlock state;
    p->getStateInformation (state);
    check (state.getSize() > 0, "estado salvo (" + juce::String ((int) state.getSize()) + " bytes)");
    for (auto* prm : prms) prm->setValueNotifyingHost (0.9f);
    p->setStateInformation (state.getData(), (int) state.getSize());
    pump (100);
    bool restored = true;
    for (auto* prm : prms)
        if (std::abs (prm->getValue() - prm->getDefaultValue()) > 1.0f) restored = false;
    float gateAfter = -1;
    for (auto* prm : prms) if (prm->getName (32).startsWith ("Gate")) gateAfter = prm->getValue();
    check (std::abs (gateAfter - 0.3f) < 0.02f, "estado restaurado (gate = " + juce::String (gateAfter, 2) + ")");
    for (auto* prm : prms) prm->setValueNotifyingHost (prm->getDefaultValue());
    check (restored, "valores dentro da faixa");

    std::cout << "== várias instâncias (como num show com vários canais)\n";
    {
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> many;
        for (int i = 0; i < 4; ++i) many.push_back (load (fm, path, 48000.0, 256));
        bool allOk = true;
        for (auto& m : many) allOk = allOk && m != nullptr;
        check (allOk, "4 instâncias ao mesmo tempo");
        for (auto& m : many) if (m) m->prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> b (2, 256);
        juce::MidiBuffer midi;
        for (int k = 0; k < 50; ++k) for (auto& m : many) { b.clear(); if (m) m->processBlock (b, midi); }
        many.clear();
        check (true, "4 instâncias destruídas sem travar");
    }

    if (argc > 3)
    {
        std::cout << "== interface\n";
        auto* ed = p->createEditorIfNeeded();
        check (ed != nullptr, "editor abre");
        if (ed != nullptr)
        {
            ed->setVisible (true);   // sem addToDesktop: a captura desenha direto numa imagem
            pump (600);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File png (juce::String::fromUTF8 (argv[3]));
            png.deleteFile();
            juce::FileOutputStream os (png);
            juce::PNGImageFormat().writeImageToStream (img, os);
            check (png.getSize() > 1000, "captura salva: " + png.getFileName());
        }
    }

    if (argc > 2 && juce::SystemStats::getEnvironmentVariable ("TAV_DEMO", {}).isNotEmpty())
    {
        std::cout << "== fluxo completo em tempo real (modo demo)\n";
        auto audio = readWav48kStereo (juce::File (juce::String::fromUTF8 (argv[2])));
        check (audio.getNumSamples() > 0, "WAV lido (" + juce::String (audio.getNumSamples() / 48000.0, 1) + " s)");
        auto logs = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("BandChatLIVE/Logs");
        auto today = logs.getChildFile (juce::Time::getCurrentTime().formatted ("transcricao_%Y-%m-%d.txt"));
        const auto before = today.loadFileAsString();

        p->prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < audio.getNumSamples(); pos += 512)
        {
            const int n = juce::jmin (512, audio.getNumSamples() - pos);
            juce::AudioBuffer<float> b (2, n);
            for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, audio, ch, pos, n);
            p->processBlock (b, midi);
            // ritmo de tempo real, como uma placa de som
            const double due = t0 + (pos + n) * 1000.0 / 48000.0;
            while (juce::Time::getMillisecondCounterHiRes() < due)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
        // 1,5 s de silêncio para fechar a última frase
        for (int k = 0; k < 150; ++k) { juce::AudioBuffer<float> b (2, 480); b.clear(); p->processBlock (b, midi); pump (10); }
        pump (1500);
        const auto added = today.loadFileAsString().substring (before.length());
        std::cout << added;
        const int lines = juce::StringArray::fromLines (added.trim()).size();
        check (added.contains ("[demo] fala de") && lines == 4, juce::String (lines) + " frases no histórico (esperado 4: o instrumento não conta)");
    }

    p->releaseResources();
    p.reset();
    std::cout << (failures == 0 ? "\nTUDO OK\n" : "\nFALHAS: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
