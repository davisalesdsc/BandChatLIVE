// Testes de console do motor (sem interface, sem host).
//
//   TavTests                       -> testes de texto
//   TavTests fala.wav              -> + segmentação com o VAD Silero embutido
//   TavTests fala.wav modelo.bin   -> + transcrição com Whisper
#include "Engine/Segmenter.h"
#include "Engine/TextFilter.h"
#include "Engine/WhisperBackend.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace tav;

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FALHOU ") << what.toRawUTF8() << "\n";
    if (! ok) ++failures;
}
static void check (bool ok, const char* what) { check (ok, juce::String::fromUTF8 (what)); }

static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

static void textTests()
{
    std::cout << "== texto\n";
    check (text::fold (u8 ("Violão ÇÃO")) == "violao cao", "fold remove acento e maiúscula");
    check (text::fold (u8 ("Ação")).length() == u8 ("Ação").length(), "fold preserva o tamanho");
    check (text::isHallucination (u8 ("Legendas pela comunidade Amara.org")), "alucinação clássica");
    check (text::isHallucination (u8 ("Obrigado por assistir!")), "obrigado por assistir");
    check (text::isHallucination (u8 ("não não não não não")), "repetição de 1 palavra");
    check (text::isHallucination (u8 ("mais um mais um mais um mais um")), "repetição de 2 palavras");
    check (! text::isHallucination (u8 ("aumenta o retorno da voz no meu fone")), "frase normal passa");
    check (text::clean (u8 ("  aumenta \n o   fone ")) == u8 ("aumenta o fone"), "clean normaliza espaços");
    check (text::clean (".") .isEmpty(), "clean descarta pontuação solta");

    auto corr = text::parseCorrections (u8 ("david => Davi ; in ear => in-ear ; violao => violão"));
    check (corr.size() == 3, "3 correções lidas");
    check (text::applyCorrections (u8 ("O David pediu in ear"), corr) == u8 ("O Davi pediu in-ear"), "correções aplicadas");
    check (text::applyCorrections (u8 ("Davidson chegou"), corr) == u8 ("Davidson chegou"), "só palavra inteira");
    check (text::applyCorrections (u8 ("o VIOLÃO tá alto"), corr) == u8 ("o violão tá alto"), "ignora acento/maiúscula");

    auto kw = text::parseKeywords (u8 ("fone, Violão , retorno"));
    check (text::containsKeyword (u8 ("abaixa o VIOLAO"), kw), "palavra-chave sem acento");
    check (! text::containsKeyword (u8 ("tudo certo"), kw), "sem palavra-chave");
}

static bool readWav16k (const juce::File& f, std::vector<float>& out)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return false;
    juce::AudioBuffer<float> buf ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&buf, 0, (int) r->lengthInSamples, 0, true, true);
    const double ratio = r->sampleRate / 16000.0;
    const int n = (int) ((double) buf.getNumSamples() / ratio);
    out.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double pos = i * ratio;
        const int a = (int) pos;
        const float frac = (float) (pos - a);
        float v = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float x0 = buf.getSample (ch, a);
            const float x1 = buf.getSample (ch, juce::jmin (a + 1, buf.getNumSamples() - 1));
            v += x0 + (x1 - x0) * frac;
        }
        out[(size_t) i] = v / (float) buf.getNumChannels();
    }
    return true;
}

static void segmentTests (const std::vector<float>& audio, const char* modelPath)
{
    std::cout << "== segmentação (" << juce::String ((double) audio.size() / 16000.0, 1) << " s)\n";
    juce::String err;
    auto vad = whisper::Vad::create (err);
    check (vad != nullptr, "VAD Silero embutido carregou " + err);

    if (vad != nullptr)
    {
        // o mesmo áudio tem de dar as mesmas probabilidades, em contexto novo e
        // qualquer que seja o tamanho do lote (a thread do plugin varia o lote)
        const int frames = juce::jmin (400, (int) audio.size() / Segmenter::kFrame);
        std::vector<float> a1 ((size_t) frames), a7 ((size_t) frames);
        auto v1 = whisper::Vad::create (err), v7 = whisper::Vad::create (err);
        for (int i = 0; i < frames; ++i) v1->process (audio.data() + (size_t) i * Segmenter::kFrame, 1, a1.data() + i);
        for (int i = 0; i < frames; i += 7) v7->process (audio.data() + (size_t) i * Segmenter::kFrame, juce::jmin (7, frames - i), a7.data() + i);
        float maxDiff = 0.0f;
        for (int i = 0; i < frames; ++i) maxDiff = juce::jmax (maxDiff, std::abs (a1[(size_t) i] - a7[(size_t) i]));
        check (maxDiff < 1e-4f, u8 ("VAD determinístico (diferença máxima ") + juce::String (maxDiff, 5) + ")");
    }

    std::unique_ptr<whisper::Session> session;
    if (modelPath != nullptr)
    {
        session = whisper::Session::create (juce::File (juce::String (modelPath)), err);
        check (session != nullptr, "modelo Whisper carregou " + err);
    }

    for (int useVad = 1; useVad >= 0; --useVad)
    {
        std::cout << (useVad ? "-- com VAD neural\n" : "-- só volume\n");
        Segmenter::Settings st;
        st.useVad = useVad && vad != nullptr;
        Segmenter seg (st);
        if (vad) vad->reset();
        std::vector<Segmenter::Event> ev;
        int transcribed = 0;
        juce::String allText;
        const int frames = (int) audio.size() / Segmenter::kFrame;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < frames; i += 4)    // em lotes de 4 quadros, como no plugin
        {
            const int n = juce::jmin (4, frames - i);
            float probs[4] = { 1, 1, 1, 1 };
            if (st.useVad) vad->process (audio.data() + (size_t) i * Segmenter::kFrame, n, probs);
            if (st.useVad && std::getenv ("TAV_DUMP_VAD") != nullptr)
                for (int k = 0; k < n; ++k)
                    if (i + k >= 300 && i + k < 360) std::cout << "vad " << (i + k) * 0.032 << " " << probs[k] << "\n";
            for (int k = 0; k < n; ++k)
                seg.processFrame (audio.data() + (size_t) (i + k) * Segmenter::kFrame, probs[k], ev);
        }
        seg.flush (ev);
        std::cout << "   (" << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) << " ms para processar)\n";
        for (auto& e : ev)
        {
            if (! e.isFinal) continue;
            std::cout << "   " << juce::String ((double) e.startSample / 16000.0, 2) << " s  "
                      << (e.audio.empty() ? juce::String ("(descartada)") : juce::String ((double) e.audio.size() / 16000.0, 1) + " s de fala");
            if (session != nullptr && ! e.audio.empty())
            {
                const auto t0w = juce::Time::getMillisecondCounterHiRes();
                const auto txt = text::clean (session->transcribe (e.audio, {}));
                const double secs = (juce::Time::getMillisecondCounterHiRes() - t0w) / 1000.0;
                std::cout << "  -> \"" << txt.toRawUTF8() << "\"  (" << juce::String (secs, 1) << " s"
                          << (session->usesGpu() ? ", GPU" : ", CPU") << ")";
                if (useVad) { transcribed += txt.isNotEmpty() ? 1 : 0; allText << txt << " "; }
            }
            std::cout << "\n";
        }
        if (session != nullptr && useVad)
        {
            // voz sintética (espeak) e modelo pequeno: não dá para exigir o texto
            // exato, mas as falas têm de virar texto e algumas palavras têm de bater
            check (transcribed >= 3, "Whisper transcreveu " + juce::String (transcribed) + " de 4 falas");
            const auto f = text::fold (allText);
            int hits = 0;
            for (auto* w : { "retorno", "fone", "violao", "monitor", "frase", "segundos", "show" })
                hits += f.contains (w) ? 1 : 0;
            check (hits >= 2, u8 ("palavras reconhecidas: ") + juce::String (hits) + " de 7");
        }
        if (useVad)
        {
            bool instrumentRejected = true;
            for (auto& e : ev)
                if (e.isFinal && ! e.audio.empty() && e.startSample > 6.0 * 16000 && e.startSample < 9.4 * 16000)
                    instrumentRejected = false;
            check (instrumentRejected, u8 ("trecho de instrumento (6,3-9,3 s) não virou frase"));
        }
    }
}

int main (int argc, char* argv[])
{
    textTests();
    if (argc > 1)
    {
        std::vector<float> audio;
        if (! readWav16k (juce::File (juce::String (argv[1])), audio)) { std::cerr << "WAV inválido\n"; return 2; }
        segmentTests (audio, argc > 2 ? argv[2] : nullptr);
    }
    std::cout << (failures == 0 ? "\nTUDO OK\n" : "\nFALHAS: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
