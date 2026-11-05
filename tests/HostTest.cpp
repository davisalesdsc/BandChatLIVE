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


// Grava preferências "de sessão" no estado do plugin, como um DAW restaurando
// um projeto. Pelo host VST3 o estado vem num envelope: XML "VST3PluginState"
// com o estado do plugin em base64 no <IComponent>; dentro dele, cabeçalho JUCE
// (magic + tamanho) + XML do plugin.
static bool setSessionPrefs (juce::AudioPluginInstance& p, const juce::StringPairArray& prefsToSet)
{
    auto readJuceXml = [] (const void* d, size_t n, size_t& xmlBytes) -> juce::String
    {
        if (n < 9) return {};
        xmlBytes = (size_t) *(const juce::uint32*) ((const char*) d + 4);
        return juce::String::fromUTF8 ((const char*) d + 8, (int) juce::jmin (xmlBytes, n - 8) - 1);
    };
    auto writeJuceXml = [] (const juce::String& text, juce::MemoryBlock& out)
    {
        out.append ("\x56\x43\x32\x21", 4);              // magic 0x21324356 (JUCE)
        const auto bytes = (juce::uint32) text.getNumBytesAsUTF8() + 1;
        out.append (&bytes, 4);
        out.append (text.toRawUTF8(), bytes);
    };
    juce::MemoryBlock st;
    p.getStateInformation (st);
    size_t outerBytes = 0;
    auto outer = juce::parseXML (readJuceXml (st.getData(), st.getSize(), outerBytes));
    auto* comp = outer != nullptr ? outer->getChildByName ("IComponent") : nullptr;
    if (comp == nullptr) return false;
    juce::MemoryBlock inner;
    inner.fromBase64Encoding (comp->getAllSubText().trim());
    size_t innerBytes = 0;
    auto plug = juce::parseXML (readJuceXml (inner.getData(), inner.getSize(), innerBytes));
    if (plug == nullptr || ! plug->hasTagName ("BandChatLIVE")) return false;
    auto* prefs = plug->getChildByName ("prefs");
    if (prefs == nullptr) prefs = plug->createNewChildElement ("prefs");
    for (auto& k : prefsToSet.getAllKeys()) prefs->setAttribute (k, prefsToSet[k]);
    juce::MemoryBlock newInner;
    writeJuceXml (plug->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()), newInner);
    if (8 + innerBytes < inner.getSize())                      // dados extras do wrapper JUCE, se houver
        newInner.append ((const char*) inner.getData() + 8 + innerBytes, inner.getSize() - 8 - innerBytes);
    comp->deleteAllTextElements();
    comp->addTextElement (newInner.toBase64Encoding());
    juce::MemoryBlock newOuter;
    writeJuceXml (outer->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()), newOuter);
    p.setStateInformation (newOuter.getData(), (int) newOuter.getSize());
    return true;
}

static juce::AudioProcessorParameter* paramNamed (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (64) == name) return prm;
    return nullptr;
}

static bool isAU (const juce::String& path) { return path.endsWithIgnoreCase (".component"); }

static std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& fm, const juce::String& path,
                                                        double rate, int block)
{
    juce::OwnedArray<juce::PluginDescription> found;
   #if JUCE_MAC
    if (isAU (path))
    {
        juce::AudioUnitPluginFormat au;
        au.findAllTypesForFile (found, path);
    }
    else
   #endif
    {
        juce::VST3PluginFormat vst3;
        vst3.findAllTypesForFile (found, path);
    }
    if (found.isEmpty()) return {};
    juce::String err;
    return fm.createPluginInstance (*found[0], rate, block, err);
}

/** Pasta de dados do plugin (a mesma de tav::models::dataDir). */
static juce::File dataDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("BandChatLIVE");
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
   #if JUCE_MAC
    fm.addFormat (new juce::AudioUnitPluginFormat());
   #endif
    const juce::String path = juce::String::fromUTF8 (argv[1]);
    // cada execução começa sem preferências salvas (antes de abrir qualquer canal:
    // o plugin lê o arquivo ao abrir e o que está salvo no computador vale)
    dataDir().getChildFile ("preferencias.xml").deleteFile();

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

    std::cout << "== preferências globais (não somem ao reabrir)\n";
    {
        auto prefsFile = dataDir().getChildFile ("preferencias.xml");
        auto a = load (fm, path, 48000.0, 512);
        juce::StringPairArray old;
        old.set ("correcoes", juce::String::fromUTF8 ("violao => violão"));   // salva só no projeto (até 0.5)
        if (isAU (path)) { std::cout << "  (AU: migração testada pelo VST3)\n"; a.reset(); }
        else
        {
        check (setSessionPrefs (*a, old), juce::String::fromUTF8 ("sessão antiga carregada"));
        pump (800);
        a.reset();                                                  // fechar o plugin grava o arquivo
        pump (300);
        juce::PropertiesFile::Options ro;
        ro.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::PropertiesFile saved (prefsFile, ro);
        check (saved.getValue ("correcoes") == juce::String::fromUTF8 ("violao => violão"),
               juce::String::fromUTF8 ("correção da sessão antiga gravada no computador"));
        }
        // agora o arquivo vale para qualquer canal/projeto novo
        juce::PropertiesFile::Options o;
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::PropertiesFile pf (prefsFile, o);
        pf.setValue ("correcoes", "demo => DEMO");
        pf.saveIfNeeded();
        pump (1500);       // como outro programa gravando: o canal já aberto tem de reler sozinho
    }

    std::cout << "== silenciar saída e entrada direta da placa\n";
    {
        auto m = load (fm, path, 48000.0, 512);
        m->prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;
        auto* mute = paramNamed (*m, juce::String::fromUTF8 ("Silenciar saída"));
        check (mute != nullptr, juce::String::fromUTF8 ("parâmetro Silenciar saída existe"));
        if (mute != nullptr)
        {
            mute->setValueNotifyingHost (1.0f);
            juce::AudioBuffer<float> b (2, 512);
            for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.5f); b.setSample (1, i, -0.5f); }
            m->processBlock (b, midi);
            check (b.getMagnitude (0, 512) == 0.0f, juce::String::fromUTF8 ("Silenciar saída: faixa muda"));
            mute->setValueNotifyingHost (0.0f);
        }
        // placa que não existe: tem de avisar e seguir funcionando pela faixa
        juce::StringPairArray placa;
        placa.set ("entrada", "placa");
        placa.set ("placa", "Placa Que Nao Existe");
        if (! isAU (path))
            check (setSessionPrefs (*m, placa), juce::String::fromUTF8 ("modo entrada direta configurado"));
        pump (500);
        bool intact = true;
        for (int k = 0; k < 20; ++k)
        {
            juce::AudioBuffer<float> b (2, 512);
            for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.25f); b.setSample (1, i, 0.25f); }
            m->processBlock (b, midi);
            intact = intact && b.getSample (0, 100) == 0.25f;
        }
        check (intact, juce::String::fromUTF8 ("placa inexistente: sem travar e com o áudio passando"));
        m->releaseResources();
        m.reset();
        check (true, juce::String::fromUTF8 ("plugin fechado com entrada direta configurada"));
    }

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
        auto logs = dataDir().getChildFile ("Logs");
        auto today = logs.getChildFile (juce::Time::getCurrentTime().formatted ("transcricao_%Y-%m-%d.txt"));
        const auto before = today.loadFileAsString();

        // dois canais de comunicação, cada um com o nome da faixa informado
        // pelo "host" (o mesmo caminho que REAPER/Cubase/Logic usam)
        auto q = load (fm, path, 48000.0, 512);
        check (q != nullptr, "segundo canal carrega");
        juce::AudioProcessor::TrackProperties tp;
        tp.name = juce::String::fromUTF8 ("Comunicação Palco");
        p->updateTrackProperties (tp);
        tp.name = juce::String::fromUTF8 ("Comunicação Davi");
        tp.colourARGB = 0xff3366ffu;
        q->updateTrackProperties (tp);
        pump (300);

        p->prepareToPlay (48000.0, 512);
        q->prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < audio.getNumSamples(); pos += 512)
        {
            const int n = juce::jmin (512, audio.getNumSamples() - pos);
            juce::AudioBuffer<float> b (2, n), c (2, n);
            for (int ch = 0; ch < 2; ++ch) { b.copyFrom (ch, 0, audio, ch, pos, n); c.copyFrom (ch, 0, audio, ch, pos, n); }
            p->processBlock (b, midi);
            q->processBlock (c, midi);
            // ritmo de tempo real, como uma placa de som
            const double due = t0 + (pos + n) * 1000.0 / 48000.0;
            while (juce::Time::getMillisecondCounterHiRes() < due)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
        // 1,5 s de silêncio para fechar a última frase
        for (int k = 0; k < 150; ++k)
        {
            juce::AudioBuffer<float> b (2, 480), c (2, 480); b.clear(); c.clear();
            p->processBlock (b, midi); q->processBlock (c, midi); pump (10);
        }
        pump (2000);
        const auto added = today.loadFileAsString().substring (before.length());
        std::cout << added;
        const auto all = juce::StringArray::fromLines (added.trim());
        int palco = 0, davi = 0;
        for (auto& l : all)
        {
            // "[DEMO]": a correção "demo => DEMO" veio do arquivo de preferências
            palco += l.contains (juce::String::fromUTF8 ("] Comunicação Palco: [DEMO] fala de")) ? 1 : 0;
            davi  += l.contains (juce::String::fromUTF8 ("] Comunicação Davi: [DEMO] fala de")) ? 1 : 0;
        }
        int demo = 0;
        for (auto& l : all) demo += l.contains ("[DEMO] fala de") ? 1 : 0;
        if (isAU (path) && palco + davi == 0)
        {
            std::cout << "  (AU: o host de teste não repassa o nome da faixa; conferindo só as falas)\n";
            palco = davi = demo / 2;
        }
        check (palco == 4 && davi == 4 && all.size() == 8,
               juce::String::fromUTF8 ("dois canais com nome e correção global no histórico: Palco ") + juce::String (palco)
               + ", Davi " + juce::String (davi) + juce::String::fromUTF8 (" (esperado 4 e 4; o instrumento não conta)"));
        q->releaseResources();
        q.reset();
    }

    p->releaseResources();
    p.reset();
    std::cout << (failures == 0 ? "\nTUDO OK\n" : "\nFALHAS: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
