#pragma once

#include "Models.h"
#include "Segmenter.h"
#include "TextFilter.h"
#include "WhisperBackend.h"

#include <juce_core/juce_core.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace tav
{

/** Motor completo de uma instância do plugin.

      thread de áudio --pushAudio()--> FIFO sem trava
      thread "ouvido"   : FIFO -> VAD Silero + nível -> Segmenter -> fila
      thread "escriba"  : fila -> Whisper -> filtros -> linhas (+ log diário)

    A thread de áudio nunca espera por nada. */
class Transcriber
{
public:
    struct Settings
    {
        juce::String language = "pt";
        float gateDb          = -48.0f;
        float vadSensitivity  = 0.5f;     // 0..1 -> limiar 0,80..0,30
        int   holdMs          = 700;
        bool  partials        = true;
        float maxDelayS       = 4.0f;
        models::Mode mode     = models::Mode::automatic;
        juce::String vocabulary;          // vira o initial_prompt
        juce::String corrections;         // "errado => certo ; ..."
        juce::String channelName;         // prefixo no log diário
        bool  writeLog        = true;
    };

    enum class LineKind { final, partial, system };

    struct Line
    {
        juce::String id;
        juce::int64  timeMs = 0;
        juce::String text;
        LineKind     kind = LineKind::final;
    };

    enum class State { noModel, loading, listening, transcribing, error };

    Transcriber();
    ~Transcriber();

    //-- thread de áudio -----------------------------------------------------
    /** Amostras mono a 16 kHz. Nunca bloqueia; descarta se a fila encher. */
    void pushAudio (const float* samples, int numSamples) noexcept;
    /** Transcrição ligada/pausada (parâmetro automatizável do host). */
    void setActive (bool shouldBeActive) noexcept { active.store (shouldBeActive); }

    //-- thread de mensagens -------------------------------------------------
    void setSettings (const Settings& s);
    Settings getSettings() const;

    /** Escolhe de novo o modelo pelo modo atual (após baixar um modelo, trocar o modo...). */
    void refreshModel();

    std::vector<Line> getLines (int& version) const;
    int  getLinesVersion() const noexcept { return linesVersion.load(); }
    void clearLines();

    State        getState() const noexcept        { return state.load(); }
    juce::String getModelLabel() const;
    juce::String getMessage() const;               // aviso/erro mais recente
    bool  isVadActive() const noexcept             { return vadOk.load(); }
    bool  isUsingGpu() const noexcept              { return usingGpu.load(); }
    bool  arePartialsOn() const noexcept           { return partialsOn.load(); }
    bool  isSpeaking() const noexcept              { return speaking.load(); }
    float getDelaySeconds() const noexcept         { return delayS.load(); }

private:
    struct Job
    {
        bool isFinal = true;
        juce::String id;
        juce::int64 timeMs = 0;
        std::vector<float> audio;    // vazio num final = descartar a parcial
    };

    class Worker : public juce::Thread
    {
    public:
        Worker (const juce::String& name, std::function<void()> f) : juce::Thread (name), fn (std::move (f)) {}
        void run() override { fn(); }
    private:
        std::function<void()> fn;
    };

    void listenLoop();
    void scribeLoop();
    void process (Job& job, int newerFinalsWaiting);
    void relieve();
    void loadModel (const juce::File& f);
    void publish (const juce::String& id, juce::int64 timeMs, const juce::String& text, LineKind kind);
    void notice (const juce::String& text);
    Segmenter::Settings segmenterSettings (const Settings& s) const;

    // fila de trabalho: finais em ordem, parcial só a mais recente
    void enqueue (Job&& j);
    bool finalPendingFor (const juce::String& id);

    // áudio
    juce::AbstractFifo fifo { 16000 * 20 };
    std::vector<float> fifoData;
    std::atomic<bool>  active { true };
    std::atomic<int>   droppedAudio { 0 };

    // configuração
    mutable std::mutex settingsLock;
    Settings settings;
    std::atomic<int> settingsVersion { 0 };

    // fila
    std::mutex qLock;
    std::condition_variable qCv;
    std::deque<Job> finals;
    std::unique_ptr<Job> partial;

    // modelo: só a thread escriba cria/usa a sessão; o lock protege a troca
    // contra o abort() vindo do destrutor
    std::unique_ptr<whisper::Session> session;
    std::mutex sessionLock;
    std::mutex modelReqLock;
    juce::File requestedModel;
    std::atomic<bool> modelRequested { false };

    // estado publicado para a interface
    std::atomic<State> state { State::noModel };
    std::atomic<bool>  vadOk { false }, usingGpu { false }, partialsOn { true }, speaking { false };
    std::atomic<float> delayS { 0.0f };
    mutable std::mutex infoLock;
    juce::String modelLabel, message;

    // alívio automático
    int  slowStreak = 0;
    std::atomic<bool> partialsDisabledByRelief { false };
    bool economic = false;
    juce::StringArray shownPartials;

    // linhas
    mutable std::mutex linesLock;
    std::vector<Line> lines;
    std::atomic<int>  linesVersion { 0 };

    std::unique_ptr<Worker> listener, scribe;
    std::atomic<bool> quitting { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Transcriber)
};

} // namespace tav
