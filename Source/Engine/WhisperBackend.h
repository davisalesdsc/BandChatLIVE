#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

struct whisper_context;
struct whisper_state;
struct whisper_vad_context;

namespace tav
{

/** Ponte com o whisper.cpp. Tudo aqui roda fora da thread de áudio. */
namespace whisper
{
    /** Liga o filtro de log do whisper/ggml (só avisos e erros) uma vez. */
    void initLogging();

    /** false (com motivo) se esta CPU não roda o binário (ex.: x86 sem AVX2). */
    bool cpuSupported (juce::String& reason);

    /** Pesos de um modelo carregados UMA vez por processo e divididos entre
        todas as instâncias do plugin que usam o mesmo arquivo. */
    struct SharedModel
    {
        whisper_context* ctx = nullptr;
        juce::File file;
        bool usesGpu = false;
        ~SharedModel();
    };

    std::shared_ptr<SharedModel> acquireModel (const juce::File& file, juce::String& error);

    /** Uma sessão de transcrição: modelo compartilhado + estado próprio. */
    class Session
    {
    public:
        struct Options
        {
            juce::String language = "pt";
            juce::String initialPrompt;
            int  threads = 4;
            bool economic = false;   // janela do encoder menor: mais rápido, erra mais
        };

        static std::unique_ptr<Session> create (const juce::File& modelFile, juce::String& error);
        ~Session();

        /** Transcreve áudio mono 16 kHz. Bloqueia; pode ser interrompido por abort(). */
        juce::String transcribe (const std::vector<float>& audio, const Options& opt);

        void abort() noexcept { aborting.store (true); }
        const juce::File& getFile() const noexcept { return model->file; }
        bool usesGpu() const noexcept { return model->usesGpu; }

    private:
        Session() = default;
        std::shared_ptr<SharedModel> model;
        whisper_state* state = nullptr;
        std::atomic<bool> aborting { false };
    };

    /** Detector de voz Silero (embutido no binário), em modo streaming. */
    class Vad
    {
    public:
        static std::unique_ptr<Vad> create (juce::String& error);
        ~Vad();

        /** n quadros de 512 amostras -> n probabilidades. false se falhar. */
        bool process (const float* samples, int numFrames, float* probsOut);
        void reset();

    private:
        Vad() = default;
        whisper_vad_context* ctx = nullptr;
    };
}

} // namespace tav
