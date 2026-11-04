#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>

namespace tav::models
{

/** Pasta de dados do produto:
      macOS   ~/Library/Application Support/TranscricaoAoVivo
      Windows %APPDATA%\TranscricaoAoVivo
      Linux   ~/.config/TranscricaoAoVivo                                     */
juce::File dataDir();
juce::File modelsDir();
/** Modelos instalados pelo instalador, para todos os usuários:
      macOS   /Library/Application Support/TranscricaoAoVivo/modelos
      Windows C:\ProgramData\TranscricaoAoVivo\modelos                         */
juce::File systemModelsDir();
juce::File logsDir();

/** Modelos Whisper (ggml-*.bin) da pasta, do mais leve para o mais pesado. */
juce::Array<juce::File> installed();

/** 0 = tiny ... 5 = large; 99 = desconhecido. */
int weight (const juce::File& f);

/** Nome pelo que o modelo entrega ("Rápido", "Preciso"...), não pelo arquivo. */
juce::String label (const juce::File& f);

enum class Mode { automatic = 0, light = 1, precise = 2 };

/** Escolhe o modelo inicial para o modo.
      automatic: o melhor que costuma acompanhar esta máquina (desce sozinho se atrasar)
      light:     o mais leve instalado (nunca muda)
      precise:   o mais pesado instalado (nunca rebaixa)                       */
juce::File choose (Mode mode);

/** Próximo modelo mais leve instalado (para o rebaixamento automático). */
juce::File lighterThan (const juce::File& current);

/** Modelos oferecidos para baixar dentro do plugin (Hugging Face, whisper.cpp). */
struct Download
{
    const char* file;
    const char* label;      // UTF-8
    const char* size;       // texto para o botão
};

const Download* catalogue (int& count);
juce::URL urlFor (const Download& d);

/** Baixa um modelo para a pasta de modelos (um por vez, por processo).
    Grava em .part e só renomeia quando termina: modelo pela metade nunca
    aparece na lista. Thread-safe; a interface consulta por timer. */
class Downloader : private juce::URL::DownloadTaskListener
{
public:
    static Downloader& get();

    bool start (const Download& d);          // false se já houver um em andamento
    void cancel();
    bool  isBusy() const;
    float getProgress() const;               // 0..1 (ou -1 se o tamanho é desconhecido)
    juce::String getCurrentLabel() const;
    juce::String getLastResult() const;      // "baixado: ..." ou "erro: ..."
    int   getFinishedCount() const noexcept  { return finishCount.load(); }

private:
    Downloader() = default;
    void progress (juce::URL::DownloadTask*, juce::int64 done, juce::int64 total) override;
    void finished_ (juce::URL::DownloadTask*, bool success);
    void finished (juce::URL::DownloadTask* t, bool success) override { finished_ (t, success); }

    mutable std::mutex lock;
    std::unique_ptr<juce::URL::DownloadTask> task;
    juce::File target, partial;
    juce::String label, lastResult;
    std::atomic<float> prog { 0.0f };
    std::atomic<bool> busy { false };
    std::atomic<int> finishCount { 0 };
};

} // namespace tav::models
