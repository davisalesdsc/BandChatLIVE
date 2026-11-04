#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace tav
{

/** Separa a fala em frases, quadro a quadro (32 ms a 16 kHz).

    Cada quadro é avaliado por NÍVEL (dBFS, acima de um piso e de um ruído de
    fundo que se adapta sozinho) e pela PROBABILIDADE DE VOZ do Silero. Uma
    frase começa com ~100 ms seguidos de voz (com 500 ms de pré-roll), fecha
    com `holdMs` de silêncio ou ao chegar em `maxSegmentS`, e enquanto a
    pessoa fala solta um evento parcial a cada `partialMs`.

    Não sabe nada de Whisper nem de threads: recebe quadros, devolve eventos.
    Por isso dá para testar com um WAV e um VAD de mentira. */
class Segmenter
{
public:
    static constexpr int kRate  = 16000;
    static constexpr int kFrame = 512;     // 32 ms

    struct Settings
    {
        float minDb        = -48.0f;  // nível mínimo (gate)
        float noiseMarginDb = 9.0f;   // acima do ruído de fundo medido
        float vadThreshold = 0.55f;   // probabilidade mínima de voz
        int   holdMs       = 700;     // silêncio que fecha a frase
        float maxSegmentS  = 10.0f;   // corta frases longas
        int   minSpeechMs  = 400;     // menos que isso é estalo/clique
        int   partialMs    = 1500;    // intervalo dos parciais
        bool  useVad       = true;    // false = só nível (+ teste de dinâmica)
    };

    struct Event
    {
        bool               isFinal = true;
        int64_t            startSample = 0;   // posição absoluta no fluxo (amostras)
        std::vector<float> audio;             // vazio num final = frase descartada
    };

    Segmenter();
    explicit Segmenter (const Settings& s);

    void setSettings (const Settings& s);
    const Settings& getSettings() const noexcept { return settings; }

    /** Um quadro de kFrame amostras + a probabilidade de voz dele (0..1).
        Eventos novos são acrescentados em `out`. */
    void processFrame (const float* frame, float vadProb, std::vector<Event>& out);

    /** Fecha a frase em andamento (fim do fluxo / transcrição pausada). */
    void flush (std::vector<Event>& out);

    void reset();

    bool  isSpeaking() const noexcept   { return speaking; }
    float getNoiseDb() const noexcept   { return noiseDb; }
    float getLastDb() const noexcept    { return lastDb; }

private:
    void emit (std::vector<Event>& out);
    int  frames (float seconds) const noexcept;

    Settings settings;
    int endFrames = 0, maxFrames = 0, minVoiced = 0, partialFrames = 0, tailFrames = 0, preFrames = 0;
    static constexpr int kStartFrames = 3;

    struct Past { int64_t index; std::vector<float> frame; float db; };
    std::deque<Past> recent;

    bool    noiseInit = false;
    float   noiseDb = -90.0f, lastDb = -100.0f;
    int64_t frameIndex = 0;
    bool    speaking = false;
    std::vector<float> cur;
    std::vector<float> curDb;
    int64_t curStartFrame = 0;
    int     voiced = 0, silence = 0, run = 0, sincePartial = 0;
    float   runPeak = 0.0f;
};

} // namespace tav
