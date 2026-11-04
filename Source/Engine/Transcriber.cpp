#include "Transcriber.h"
#include "../Common/TranscriptLog.h"

namespace tav
{

static constexpr int kRate = Segmenter::kRate;
static constexpr int kFrame = Segmenter::kFrame;
static constexpr int kSlowStreak = 3;          // frases seguidas atrasadas antes de aliviar
static constexpr size_t kMaxLines = 3000;

static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

/** TAV_DEMO=1: não usa o Whisper; cada fala vira "[demo] fala de X s".
    Serve para testar captura, segmentação, fila, tela e log sem modelo. */
static bool demoMode()
{
    static const bool on = juce::SystemStats::getEnvironmentVariable ("TAV_DEMO", {}).isNotEmpty();
    return on;
}

Transcriber::Transcriber()
{
    fifoData.resize ((size_t) fifo.getTotalSize());
    listener = std::make_unique<Worker> ("TAV ouvido", [this] { listenLoop(); });
    scribe   = std::make_unique<Worker> ("TAV escriba", [this] { scribeLoop(); });
    listener->startThread (juce::Thread::Priority::normal);
    scribe->startThread (juce::Thread::Priority::normal);
    refreshModel();
}

Transcriber::~Transcriber()
{
    quitting.store (true);
    qCv.notify_all();
    {
        std::lock_guard<std::mutex> g (sessionLock);
        if (auto* s = session.get()) s->abort();
    }
    listener->stopThread (3000);
    scribe->stopThread (8000);
    session.reset();
}

//==============================================================================
void Transcriber::pushAudio (const float* samples, int numSamples) noexcept
{
    if (! active.load()) return;
    const auto scope = fifo.write (numSamples);
    if (scope.blockSize1 + scope.blockSize2 < numSamples)
        droppedAudio.fetch_add (numSamples - scope.blockSize1 - scope.blockSize2);
    if (scope.blockSize1 > 0) std::memcpy (fifoData.data() + scope.startIndex1, samples, sizeof (float) * (size_t) scope.blockSize1);
    if (scope.blockSize2 > 0) std::memcpy (fifoData.data() + scope.startIndex2, samples + scope.blockSize1, sizeof (float) * (size_t) scope.blockSize2);
}

void Transcriber::setSettings (const Settings& s)
{
    bool modeChanged;
    {
        std::lock_guard<std::mutex> g (settingsLock);
        modeChanged = s.mode != settings.mode;
        settings = s;
    }
    if (! s.partials) partialsOn.store (false);
    else if (! partialsDisabledByRelief.load()) partialsOn.store (true);
    settingsVersion.fetch_add (1);
    if (modeChanged) refreshModel();
}

Transcriber::Settings Transcriber::getSettings() const
{
    std::lock_guard<std::mutex> g (settingsLock);
    return settings;
}

void Transcriber::refreshModel()
{
    const auto f = models::choose (getSettings().mode);
    std::lock_guard<std::mutex> g (modelReqLock);
    requestedModel = f;
    modelRequested.store (true);
    qCv.notify_all();
}

juce::String Transcriber::getModelLabel() const { std::lock_guard<std::mutex> g (infoLock); return modelLabel; }
juce::String Transcriber::getMessage() const    { std::lock_guard<std::mutex> g (infoLock); return message; }

Segmenter::Settings Transcriber::segmenterSettings (const Settings& s) const
{
    Segmenter::Settings g;
    g.minDb        = s.gateDb;
    g.vadThreshold = juce::jlimit (0.15f, 0.95f, 0.8f - 0.5f * s.vadSensitivity);
    g.holdMs       = s.holdMs;
    g.partialMs    = 1500;
    g.maxSegmentS  = 10.0f;
    g.minSpeechMs  = 400;
    return g;
}

//==============================================================================
// thread "ouvido": FIFO -> VAD -> segmentação (leve, sempre em dia)
//==============================================================================
void Transcriber::listenLoop()
{
    juce::String err;
    auto vad = whisper::Vad::create (err);
    vadOk.store (vad != nullptr);
    if (vad == nullptr)
        notice (u8 ("Detector de voz neural indisponível (") + err + u8 ("). Usando só o volume."));

    Segmenter seg;
    int seenVersion = -1;
    bool wasActive = true;
    juce::int64 consumed = 0;                 // amostras já entregues ao segmentador
    std::vector<float> pending;               // sobra < 1 quadro
    std::vector<float> chunk;
    std::vector<float> probs;
    std::vector<Segmenter::Event> events;
    const int instanceTag = juce::Random::getSystemRandom().nextInt (1 << 20);

    while (! listener->threadShouldExit() && ! quitting.load())
    {
        if (settingsVersion.load() != seenVersion)
        {
            seenVersion = settingsVersion.load();
            auto ss = segmenterSettings (getSettings());
            ss.useVad = vad != nullptr;
            seg.setSettings (ss);
        }

        const bool isActive = active.load();
        if (! isActive && wasActive)
        {
            seg.flush (events);                // pausou no meio de uma frase
            if (vad) vad->reset();
        }
        wasActive = isActive;

        const int ready = fifo.getNumReady();
        if (ready > 0)
        {
            const auto scope = fifo.read (ready);
            const size_t old = pending.size();
            pending.resize (old + (size_t) ready);
            if (scope.blockSize1 > 0) std::memcpy (pending.data() + old, fifoData.data() + scope.startIndex1, sizeof (float) * (size_t) scope.blockSize1);
            if (scope.blockSize2 > 0) std::memcpy (pending.data() + old + (size_t) scope.blockSize1, fifoData.data() + scope.startIndex2, sizeof (float) * (size_t) scope.blockSize2);
        }

        const int nFrames = (int) (pending.size() / kFrame);
        if (nFrames > 0)
        {
            probs.assign ((size_t) nFrames, 1.0f);
            if (vad != nullptr && ! vad->process (pending.data(), nFrames, probs.data()))
            {
                vad.reset();
                vadOk.store (false);
                auto ss = seg.getSettings(); ss.useVad = false; seg.setSettings (ss);
                notice (u8 ("Detector de voz neural falhou. Usando só o volume."));
                probs.assign ((size_t) nFrames, 1.0f);
            }
            for (int i = 0; i < nFrames; ++i)
                seg.processFrame (pending.data() + (size_t) i * kFrame, probs[(size_t) i], events);
            consumed += (juce::int64) nFrames * kFrame;
            pending.erase (pending.begin(), pending.begin() + (ptrdiff_t) nFrames * kFrame);
        }
        speaking.store (seg.isSpeaking());

        if (! events.empty())
        {
            // horário real de cada fala: agora, menos o que ainda está na fila
            const auto now = juce::Time::currentTimeMillis();
            const juce::int64 behind = fifo.getNumReady() + (juce::int64) pending.size();
            for (auto& e : events)
            {
                Job j;
                j.isFinal = e.isFinal;
                j.id      = juce::String (instanceTag) + "-" + juce::String (e.startSample);
                j.timeMs  = now - (consumed + behind - e.startSample) * 1000 / kRate;
                j.audio   = std::move (e.audio);
                enqueue (std::move (j));
            }
            events.clear();
        }

        if (const int lost = droppedAudio.exchange (0); lost > 0)
            debugLog ("audio", "fila cheia: " + juce::String (lost) + " amostras descartadas");

        if (fifo.getNumReady() < kFrame)
            juce::Thread::sleep (8);
    }
}

void Transcriber::enqueue (Job&& j)
{
    {
        std::lock_guard<std::mutex> g (qLock);
        if (j.isFinal)
        {
            if (partial && partial->id == j.id) partial.reset();
            finals.push_back (std::move (j));
        }
        else
        {
            partial = std::make_unique<Job> (std::move (j));
        }
    }
    qCv.notify_one();
}

bool Transcriber::finalPendingFor (const juce::String& id)
{
    std::lock_guard<std::mutex> g (qLock);
    for (auto& f : finals) if (f.id == id) return true;
    return false;
}

//==============================================================================
// thread "escriba": Whisper (pesado). Também é quem carrega/troca modelo.
//==============================================================================
void Transcriber::loadModel (const juce::File& f)
{
    if (demoMode())
    {
        state.store (State::listening);
        std::lock_guard<std::mutex> g (infoLock);
        modelLabel = "Demo";
        return;
    }
    { std::lock_guard<std::mutex> g (sessionLock); session.reset(); }
    usingGpu.store (false);
    economic = false;
    slowStreak = 0;
    if (f == juce::File())
    {
        state.store (State::noModel);
        std::lock_guard<std::mutex> g (infoLock);
        modelLabel = {};
        message = u8 ("Nenhum modelo de voz instalado. Abra AVANÇADO e baixe um modelo.");
        return;
    }

    state.store (State::loading);
    { std::lock_guard<std::mutex> g (infoLock); modelLabel = models::label (f); }

    juce::String err;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto created = whisper::Session::create (f, err);
    if (quitting.load()) return;
    { std::lock_guard<std::mutex> g (sessionLock); session = std::move (created); }
    if (session == nullptr)
    {
        state.store (State::error);
        notice (err);
        return;
    }
    usingGpu.store (session->usesGpu());
    // Sem GPU, a janela do encoder do tamanho da frase (piso ~15 s) já desde o
    // início: medido 2-3x mais rápido com texto praticamente igual (benchmark).
    economic = ! session->usesGpu();
    debugLog ("modelo", f.getFileName() + " pronto em "
              + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) + " s");
    state.store (State::listening);
    std::lock_guard<std::mutex> g (infoLock);
    message = {};
}

void Transcriber::scribeLoop()
{
    while (! scribe->threadShouldExit() && ! quitting.load())
    {
        if (modelRequested.exchange (false))
        {
            juce::File f;
            { std::lock_guard<std::mutex> g (modelReqLock); f = requestedModel; }
            if (demoMode() || session == nullptr || f != session->getFile())
                loadModel (f);
        }

        Job job;
        int newer = 0;
        bool got = false;
        {
            std::unique_lock<std::mutex> lk (qLock);
            qCv.wait_for (lk, std::chrono::milliseconds (200), [this]
            {
                return quitting.load() || modelRequested.load() || ! finals.empty() || partial != nullptr;
            });
            if (! finals.empty())
            {
                job = std::move (finals.front());
                finals.pop_front();
                newer = (int) finals.size();
                got = true;
            }
            else if (partial != nullptr)
            {
                job = std::move (*partial);
                partial.reset();
                got = true;
            }
        }
        if (got) process (job, newer);
    }
}

void Transcriber::process (Job& job, int newerFinalsWaiting)
{
    const auto s = getSettings();

    if (job.isFinal && job.audio.empty())                // o segmentador descartou
    {
        if (shownPartials.contains (job.id)) { shownPartials.removeString (job.id); publish (job.id, job.timeMs, {}, LineKind::final); }
        return;
    }
    if (session == nullptr && ! demoMode()) return;      // sem modelo: nada a fazer

    const double durS = (double) job.audio.size() / kRate;
    const auto   endMs = job.timeMs + (juce::int64) (durS * 1000.0);
    const double ageS = (double) (juce::Time::currentTimeMillis() - endMs) / 1000.0;

    if (! job.isFinal)
    {
        if (! partialsOn.load() || finalPendingFor (job.id)) return;
    }
    else if (ageS > s.maxDelayS && newerFinalsWaiting > 0)
    {
        // velha demais e já existe fala mais nova: num show, texto atrasado engana
        if (shownPartials.contains (job.id)) { shownPartials.removeString (job.id); publish (job.id, job.timeMs, {}, LineKind::final); }
        notice (u8 ("Transcrição atrasada: fala antiga descartada."));
        return;
    }

    whisper::Session::Options opt;
    opt.language = s.language;
    opt.threads  = juce::jlimit (1, 8, juce::SystemStats::getNumCpus());   // threads lógicas: ~5-10% melhor (benchmark)
    opt.economic = economic;
    if (s.vocabulary.trim().isNotEmpty())
        opt.initialPrompt = u8 ("Conversa de palco entre músicos e técnico de som. Termos: ") + s.vocabulary.trim() + ".";

    state.store (State::transcribing);
    juce::String raw;
    if (demoMode()) { juce::Thread::sleep (200); raw = "[demo] fala de " + juce::String (durS, 1) + " s"; }
    else            raw = session->transcribe (job.audio, opt);
    state.store (State::listening);
    if (quitting.load()) return;

    auto text = text::clean (raw);

    if (! job.isFinal)
    {
        if (text.isNotEmpty() && ! finalPendingFor (job.id))
        {
            if (! shownPartials.contains (job.id)) shownPartials.add (job.id);
            while (shownPartials.size() > 50) shownPartials.remove (0);
            publish (job.id, job.timeMs, text, LineKind::partial);
        }
        return;
    }

    text = text::applyCorrections (text, text::parseCorrections (s.corrections));
    const float delay = (float) ((juce::Time::currentTimeMillis() - endMs) / 1000.0);
    delayS.store (juce::jmax (0.0f, delay));

    if (text.isNotEmpty() || shownPartials.contains (job.id))
        publish (job.id, job.timeMs, text, LineKind::final);
    shownPartials.removeString (job.id);
    if (text.isNotEmpty() && s.writeLog)
        appendTranscript (job.timeMs, s.channelName, text);

    // "não acompanha" = várias frases seguidas saindo com atraso acima do limite
    slowStreak = delay > s.maxDelayS ? slowStreak + 1 : 0;
    if (slowStreak >= kSlowStreak) { slowStreak = 0; relieve(); }
}

void Transcriber::relieve()
{
    const auto s = getSettings();
    if (partialsOn.load())
    {
        partialsOn.store (false);
        partialsDisabledByRelief.store (true);
        notice (u8 ("Computador não está acompanhando: parciais desligadas."));
        return;
    }
    if (s.mode == models::Mode::automatic && session != nullptr && ! demoMode())
    {
        const auto lighter = models::lighterThan (session->getFile());
        if (lighter != juce::File())
        {
            const auto from = models::label (session->getFile());
            loadModel (lighter);
            notice (u8 ("Computador não está acompanhando: modelo trocado de \"") + from
                    + u8 ("\" para \"") + models::label (lighter) + "\".");
            return;
        }
    }
    if (! economic && s.mode != models::Mode::precise)
    {
        economic = true;
        notice (u8 ("Computador não está acompanhando: modo econômico ligado (mais rápido, erra mais)."));
        return;
    }
    notice (u8 ("Transcrição atrasada. Feche outros programas ou escolha o modo Leve."));
}

//==============================================================================
void Transcriber::publish (const juce::String& id, juce::int64 timeMs, const juce::String& text, LineKind kind)
{
    {
        std::lock_guard<std::mutex> g (linesLock);
        int found = -1;
        if (kind != LineKind::system)
            for (int i = (int) lines.size() - 1; i >= juce::jmax (0, (int) lines.size() - 40); --i)
                if (lines[(size_t) i].id == id) { found = i; break; }

        if (found >= 0)
        {
            if (text.isEmpty()) lines.erase (lines.begin() + found);
            else { lines[(size_t) found].text = text; lines[(size_t) found].kind = kind; }
        }
        else if (text.isNotEmpty())
        {
            lines.push_back ({ id, timeMs, text, kind });
            if (lines.size() > kMaxLines) lines.erase (lines.begin(), lines.begin() + (ptrdiff_t) (lines.size() - kMaxLines));
        }
    }
    linesVersion.fetch_add (1);
}

void Transcriber::notice (const juce::String& text)
{
    {
        std::lock_guard<std::mutex> g (infoLock);
        if (message == text) return;     // não repete o mesmo aviso em sequência
        message = text;
    }
    debugLog ("aviso", text);
    const auto now = juce::Time::currentTimeMillis();
    publish ("s" + juce::String (now), now, text, LineKind::system);
}

std::vector<Transcriber::Line> Transcriber::getLines (int& version) const
{
    std::lock_guard<std::mutex> g (linesLock);
    version = linesVersion.load();
    return lines;
}

void Transcriber::clearLines()
{
    { std::lock_guard<std::mutex> g (linesLock); lines.clear(); }
    linesVersion.fetch_add (1);
}

} // namespace tav
