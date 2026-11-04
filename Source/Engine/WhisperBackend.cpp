#include "WhisperBackend.h"
#include "../Common/TranscriptLog.h"

#include <BinaryData.h>
#include <whisper.h>
#include <ggml-backend.h>

#include <atomic>
#include <cmath>
#include <map>
#include <mutex>

namespace tav::whisper
{

//==============================================================================
static void logCallback (ggml_log_level level, const char* text, void*)
{
    if (level != GGML_LOG_LEVEL_ERROR && level != GGML_LOG_LEVEL_WARN)
        return;

    // Uma falha de GPU já gerou milhares de linhas (o erro de compilação do
    // Metal inteiro) e encheu 3 MB de log. Só a 1ª linha de cada mensagem, até
    // 300 caracteres, e no máximo 60 mensagens do whisper por sessão.
    static std::atomic<int> count { 0 };
    const int n = ++count;
    if (n > 60)
    {
        if (n == 61) debugLog ("whisper", "(mais mensagens do whisper omitidas)");
        return;
    }
    auto msg = juce::String::fromUTF8 (text).trim().upToFirstOccurrenceOf ("\n", false, false);
    if (msg.length() > 300) msg = msg.substring (0, 300) + "...";
    debugLog ("whisper", msg);
}

/** A GPU funciona de verdade? Liga o backend num teste isolado, uma vez por
    processo. Sem isto, uma GPU que recusa o código do ggml deixava o modelo
    meio preso a ela e o ggml abortava o programa inteiro (o DAW fechava). */
static bool gpuUsable()
{
    static const bool ok = []
    {
        for (size_t i = 0; i < ggml_backend_dev_count(); ++i)
        {
            auto* dev = ggml_backend_dev_get (i);
            const auto type = ggml_backend_dev_type (dev);
            if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU)
                continue;
            if (auto* backend = ggml_backend_dev_init (dev, nullptr))
            {
                ggml_backend_free (backend);
                debugLog ("modelo", juce::String ("GPU ok: ") + ggml_backend_dev_name (dev));
                return true;
            }
            debugLog ("modelo", juce::String ("GPU recusou o motor (") + ggml_backend_dev_name (dev)
                                  + juce::String::fromUTF8 ("): usando só o processador"));
        }
        return false;
    }();
    return ok;
}

void initLogging()
{
    static std::once_flag once;
    std::call_once (once, [] { whisper_log_set (logCallback, nullptr); });
}

bool cpuSupported (juce::String& reason)
{
   #if TAV_NEEDS_AVX2
    if (! juce::SystemStats::hasAVX2())
    {
        reason = juce::String::fromUTF8 ("Este processador não tem AVX2 (anterior a 2013) "
                                         "e não consegue transcrever em tempo real.");
        return false;
    }
   #endif
    juce::ignoreUnused (reason);
    return true;
}

//==============================================================================
SharedModel::~SharedModel()
{
    if (ctx != nullptr) whisper_free (ctx);
}

std::shared_ptr<SharedModel> acquireModel (const juce::File& file, juce::String& error)
{
    static std::mutex lock;
    static std::map<juce::String, std::weak_ptr<SharedModel>> cache;

    std::lock_guard<std::mutex> g (lock);
    const auto key = file.getFullPathName();
    if (auto existing = cache[key].lock())
        return existing;

    initLogging();
    if (! cpuSupported (error)) return {};
    if (! file.existsAsFile()) { error = juce::String::fromUTF8 ("Arquivo de modelo não encontrado: ") + file.getFileName(); return {}; }

    auto cp = whisper_context_default_params();
    cp.use_gpu    = gpuUsable();     // Metal no Apple Silicon; nos outros não há GPU compilada
    cp.flash_attn = cp.use_gpu;
    // TAV_FLASH=0/1 força o flash attention (só para benchmark)
    if (const auto fa = juce::SystemStats::getEnvironmentVariable ("TAV_FLASH", {}); fa.isNotEmpty())
        cp.flash_attn = fa == "1";

    auto* ctx = whisper_init_from_file_with_params_no_state (file.getFullPathName().toRawUTF8(), cp);
    if (ctx == nullptr && cp.use_gpu)
    {
        // GPU indisponível (Mac antigo / VM): tenta só na CPU antes de desistir
        cp.use_gpu = cp.flash_attn = false;
        ctx = whisper_init_from_file_with_params_no_state (file.getFullPathName().toRawUTF8(), cp);
    }
    if (ctx == nullptr) { error = juce::String::fromUTF8 ("Não consegui abrir o modelo ") + file.getFileName(); return {}; }

    auto m = std::make_shared<SharedModel>();
    m->ctx = ctx;
    m->file = file;
    m->usesGpu = cp.use_gpu;
    cache[key] = m;
    debugLog ("modelo", "carregado " + file.getFileName() + (m->usesGpu ? " (GPU)" : " (CPU)"));
    return m;
}

//==============================================================================
std::unique_ptr<Session> Session::create (const juce::File& modelFile, juce::String& error)
{
    auto model = acquireModel (modelFile, error);
    if (model == nullptr) return {};

    auto* st = whisper_init_state (model->ctx);
    if (st == nullptr) { error = juce::String::fromUTF8 ("Memória insuficiente para o modelo ") + modelFile.getFileName(); return {}; }

    std::unique_ptr<Session> s (new Session());
    s->model = std::move (model);
    s->state = st;
    return s;
}

Session::~Session()
{
    if (state != nullptr) whisper_free_state (state);
}

static bool abortCallback (void* user)
{
    return static_cast<std::atomic<bool>*> (user)->load();
}

juce::String Session::transcribe (const std::vector<float>& input, const Options& opt)
{
    if (input.empty()) return {};

    // levanta o ganho de falas baixas (talkback costuma vir fraco) e completa
    // até 1,1 s: o whisper recusa trechos curtos demais
    double sum = 0.0;
    for (auto v : input) sum += (double) v * v;
    const float rms  = (float) std::sqrt (sum / (double) input.size()) + 1e-9f;
    const float gain = juce::jlimit (1.0f, 20.0f, 0.08f / rms);

    std::vector<float> audio (std::max<size_t> (input.size(), (size_t) (16000 * 1.1)), 0.0f);
    for (size_t i = 0; i < input.size(); ++i)
        audio[i] = juce::jlimit (-1.0f, 1.0f, input[i] * gain);

    auto p = whisper_full_default_params (WHISPER_SAMPLING_GREEDY);
    p.n_threads        = juce::jmax (1, opt.threads);
    p.translate        = false;
    p.no_context       = true;
    p.no_timestamps    = true;
    p.single_segment   = false;
    p.print_special    = false;
    p.print_progress   = false;
    p.print_realtime   = false;
    p.print_timestamps = false;
    p.suppress_blank   = true;
    p.suppress_nst     = true;
    p.temperature      = 0.0f;
    p.temperature_inc  = 0.0f;   // sem re-tentativas: latência previsível
    p.greedy.best_of   = 1;
    p.no_speech_thold  = 0.6f;
    p.logprob_thold    = -1.0f;
    p.entropy_thold    = 2.4f;

    const auto lang = opt.language.isNotEmpty() ? opt.language : juce::String ("pt");
    p.language = lang.toRawUTF8();
    const auto prompt = opt.initialPrompt;
    p.initial_prompt = prompt.isNotEmpty() ? prompt.toRawUTF8() : nullptr;

    if (opt.economic)
    {
        // 1500 quadros = 30 s; usar só o necessário (com piso) acelera o encoder
        const int frames = (int) std::ceil ((double) audio.size() / 16000.0 * 50.0) + 64;
        p.audio_ctx = juce::jlimit (juce::jlimit (256, 1500, opt.audioCtxFloor), 1500, frames);
    }

    aborting.store (false);
    p.abort_callback = abortCallback;
    p.abort_callback_user_data = &aborting;

    if (whisper_full_with_state (model->ctx, state, p, audio.data(), (int) audio.size()) != 0)
        return {};

    juce::String result;
    const int n = whisper_full_n_segments_from_state (state);
    for (int i = 0; i < n; ++i)
    {
        // descarta segmento com cara de "não é fala" E baixa confiança
        const float noSpeech = whisper_full_get_segment_no_speech_prob_from_state (state, i);
        const int nt = whisper_full_n_tokens_from_state (state, i);
        double logSum = 0.0; int counted = 0;
        for (int t = 0; t < nt; ++t)
        {
            const auto td = whisper_full_get_token_data_from_state (state, i, t);
            if (td.id >= whisper_token_eot (model->ctx)) continue;   // tokens especiais
            logSum += td.plog; ++counted;
        }
        const double avgLog = counted > 0 ? logSum / counted : -10.0;
        if (noSpeech > 0.6f && avgLog < -0.8) continue;
        result << juce::String::fromUTF8 (whisper_full_get_segment_text_from_state (state, i)) << " ";
    }
    return result.trim();
}

//==============================================================================
namespace
{
    struct MemReader { const char* data; size_t size; size_t pos; };

    size_t memRead (void* c, void* out, size_t n)
    {
        auto* r = static_cast<MemReader*> (c);
        n = std::min (n, r->size - r->pos);
        std::memcpy (out, r->data + r->pos, n);
        r->pos += n;
        return n;
    }
    bool memEof (void* c)  { auto* r = static_cast<MemReader*> (c); return r->pos >= r->size; }
    void memClose (void*)  {}
}

std::unique_ptr<Vad> Vad::create (juce::String& error)
{
    initLogging();
    if (! cpuSupported (error)) return {};

    MemReader reader { BinaryData::silero_vad_bin, (size_t) BinaryData::silero_vad_binSize, 0 };
    whisper_model_loader loader { &reader, memRead, memEof, memClose };

    auto vp = whisper_vad_default_context_params();
    vp.n_threads = 1;
    vp.use_gpu = false;   // modelo minúsculo: na CPU é mais rápido que ir à GPU

    auto* ctx = whisper_vad_init_with_params (&loader, vp);
    if (ctx == nullptr) { error = juce::String::fromUTF8 ("Não consegui iniciar o detector de voz."); return {}; }

    // O whisper.cpp NÃO zera o estado da LSTM ao criar o contexto: sem isto as
    // probabilidades começam de lixo de memória e o mesmo áudio dá resultados
    // diferentes a cada abertura do plugin (medido: até 0,8 de diferença).
    whisper_vad_reset_state (ctx);

    std::unique_ptr<Vad> v (new Vad());
    v->ctx = ctx;
    return v;
}

Vad::~Vad()
{
    if (ctx != nullptr) whisper_vad_free (ctx);
}

bool Vad::process (const float* samples, int numFrames, float* probsOut)
{
    if (numFrames <= 0) return true;
    if (! whisper_vad_detect_speech_no_reset (ctx, samples, numFrames * 512))
        return false;
    const int n = whisper_vad_n_probs (ctx);
    const float* p = whisper_vad_probs (ctx);
    for (int i = 0; i < numFrames; ++i)
        probsOut[i] = i < n ? p[i] : 0.0f;
    return true;
}

void Vad::reset()
{
    whisper_vad_reset_state (ctx);
}

} // namespace tav::whisper
