#include "Models.h"

namespace tav::models
{

juce::File dataDir()
{
   #if JUCE_MAC
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Application Support");
   #else
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #endif
    auto d = base.getChildFile ("BandChatLIVE");
    d.createDirectory();
    return d;
}

juce::File modelsDir() { auto d = dataDir().getChildFile ("modelos"); d.createDirectory(); return d; }

juce::File systemModelsDir()
{
   #if JUCE_MAC
    return juce::File ("/Library/Application Support/BandChatLIVE/modelos");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)   // C:\ProgramData
               .getChildFile ("BandChatLIVE").getChildFile ("modelos");
   #else
    return juce::File ("/usr/local/share/BandChatLIVE/modelos");
   #endif
}
juce::File logsDir()   { auto d = dataDir().getChildFile ("Logs");    d.createDirectory(); return d; }

namespace
{
    struct Profile { const char* key; const char* label; int weight; };

    // "turbo" antes de "large": "ggml-large-v3-turbo" contém as duas palavras
    const Profile kProfiles[] = {
        { "tiny",   "Ultrarr\xc3\xa1pido (erra muito)", 0 },
        { "base",   "R\xc3\xa1pido",                    1 },
        { "small",  "Equilibrado",                      2 },
        { "medium", "Preciso",                          3 },
        { "turbo",  "Mais preciso",                     4 },
        { "large",  "M\xc3\xa1ximo (pesado)",           5 },
    };

    const Profile* profileOf (const juce::File& f)
    {
        const auto n = f.getFileName().toLowerCase();
        for (auto& p : kProfiles)
            if (n.contains (p.key)) return &p;
        return nullptr;
    }

}

bool isAppleSilicon()
{
   #if JUCE_MAC && JUCE_ARM
    return true;
   #else
    return false;
   #endif
}

bool partialsByDefault()
{
    // cada parcial repete o trabalho todo do Whisper: em CPU de 2 núcleos
    // (ex.: Mac mini 2014) eles atrasavam as frases finais
    return isAppleSilicon() || juce::SystemStats::getNumPhysicalCpus() >= 4;
}

int weight (const juce::File& f)
{
    if (auto* p = profileOf (f)) return p->weight;
    return 99;
}

juce::String label (const juce::File& f)
{
    if (auto* p = profileOf (f)) return juce::String::fromUTF8 (p->label);
    return f.getFileNameWithoutExtension();
}

juce::Array<juce::File> installed()
{
    // pasta do usuário (downloads feitos pelo plugin) + pasta do sistema
    // (modelos que o instalador deixou para todos os usuários). Mesmo arquivo
    // nas duas: vale o do usuário.
    juce::Array<juce::File> out;
    juce::StringArray seen;
    for (auto dir : { modelsDir(), systemModelsDir() })
    {
        if (! dir.isDirectory()) continue;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "ggml-*.bin"))
        {
            const auto n = f.getFileName().toLowerCase();
            if (n.contains ("silero") || n.contains ("vad") || f.getSize() < 10 * 1024 * 1024)
                continue;                     // VAD ou download incompleto
            if (seen.contains (n)) continue;
            seen.add (n);
            out.add (f);
        }
    }
    std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b)
    {
        const int wa = weight (a), wb = weight (b);
        return wa != wb ? wa < wb : a.getFileName() < b.getFileName();
    });
    return out;
}

juce::File choose (Mode mode)
{
    const auto list = installed();
    if (list.isEmpty()) return {};

    if (mode == Mode::light)   return list.getFirst();

    // o mais pesado conhecido (modelo desconhecido não vira "o mais pesado")
    juce::File heaviest;
    for (auto& f : list)
        if (weight (f) < 99) heaviest = f;
    if (heaviest == juce::File()) heaviest = list.getLast();

    if (mode == Mode::precise) return heaviest;

    // automático: Apple Silicon (com GPU) aguenta o mais pesado.
    // Sem GPU, medido (benchmark, CPU de 2 núcleos/4 threads): "Equilibrado"
    // leva ~3,3 s por fala, lento demais ao vivo; "Rápido" ~0,6 s. Só CPU
    // grande (8+ núcleos) começa no Equilibrado.
    if (isAppleSilicon()) return heaviest;
    const int maxWeight = juce::SystemStats::getNumPhysicalCpus() >= 8 ? 2 : 1;
    juce::File best;
    for (auto& f : list)
        if (weight (f) <= maxWeight) best = f;
    return best != juce::File() ? best : list.getFirst();
}

juce::File lighterThan (const juce::File& current)
{
    const int w = weight (current);
    juce::File best;
    for (auto& f : installed())
        if (weight (f) < w && f != current) best = f;   // lista vem do leve ao pesado: fica o maior abaixo
    return best;
}

const Download* catalogue (int& count)
{
    // versões quantizadas: quase a mesma precisão em uma fração do tamanho
    // Apple Silicon (GPU): q5 é o melhor custo. Só CPU (Intel/AMD): q8_0 é
    // ~25% mais rápido que q5_1 (benchmark) e o "Mais preciso" não acompanha.
    static const Download gpu[] = {
        { "ggml-base-q5_1.bin",           "R\xc3\xa1pido",     "60 MB"  },
        { "ggml-small-q5_1.bin",          "Equilibrado",       "190 MB" },
        { "ggml-large-v3-turbo-q5_0.bin", "Mais preciso",      "550 MB" },
    };
    static const Download cpu[] = {
        { "ggml-base-q8_0.bin",           "R\xc3\xa1pido",     "80 MB"  },
        { "ggml-small-q5_1.bin",          "Equilibrado",       "190 MB" },
    };
    if (isAppleSilicon()) { count = (int) juce::numElementsInArray (gpu); return gpu; }
    count = (int) juce::numElementsInArray (cpu);
    return cpu;
}

juce::URL urlFor (const Download& d)
{
    return juce::URL ("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" + juce::String (d.file));
}

//==============================================================================
Downloader& Downloader::get()
{
    static Downloader d;
    return d;
}

bool Downloader::start (const Download& d)
{
    std::lock_guard<std::mutex> g (lock);
    if (busy.load()) return false;
    target  = modelsDir().getChildFile (d.file);
    partial = target.withFileExtension (".part");
    partial.deleteFile();
    label = juce::String::fromUTF8 (d.label);
    prog.store (0.0f);
    busy.store (true);
    task = urlFor (d).downloadToFile (partial, juce::URL::DownloadTaskOptions()
                                                  .withListener (this)
                                                  .withUsePost (false));
    if (task == nullptr)
    {
        busy.store (false);
        lastResult = juce::String::fromUTF8 ("erro: não consegui iniciar o download (internet?)");
        return false;
    }
    return true;
}

void Downloader::cancel()
{
    std::unique_ptr<juce::URL::DownloadTask> t;
    {
        std::lock_guard<std::mutex> g (lock);
        t = std::move (task);
    }
    t.reset();                                // destrói (para) o download
    partial.deleteFile();
    busy.store (false);
    std::lock_guard<std::mutex> g (lock);
    lastResult = "cancelado";
}

bool  Downloader::isBusy() const      { return busy.load(); }
float Downloader::getProgress() const { return prog.load(); }
juce::String Downloader::getCurrentLabel() const { std::lock_guard<std::mutex> g (lock); return label; }
juce::String Downloader::getLastResult() const   { std::lock_guard<std::mutex> g (lock); return lastResult; }

void Downloader::progress (juce::URL::DownloadTask*, juce::int64 done, juce::int64 total)
{
    prog.store (total > 0 ? (float) ((double) done / (double) total) : -1.0f);
}

void Downloader::finished_ (juce::URL::DownloadTask* t, bool success)
{
    std::lock_guard<std::mutex> g (lock);
    const bool httpOk = t == nullptr || t->statusCode() == 0 || (t->statusCode() >= 200 && t->statusCode() < 300);
    if (success && httpOk && partial.getSize() > 10 * 1024 * 1024)
    {
        target.deleteFile();
        partial.moveFileTo (target);
        lastResult = juce::String::fromUTF8 ("baixado: ") + label;
        finishCount.fetch_add (1);
    }
    else
    {
        partial.deleteFile();
        lastResult = juce::String::fromUTF8 ("erro ao baixar ") + label
                   + (t != nullptr && t->statusCode() > 0 ? " (HTTP " + juce::String (t->statusCode()) + ")" : juce::String());
    }
    busy.store (false);
}

} // namespace tav::models
