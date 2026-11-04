// Benchmark do motor: para cada modelo, threads e janela do encoder, mede o
// tempo de transcrever as falas do WAV e mostra o texto (para ver o custo em
// qualidade). Serve para escolher os padrões de cada tipo de máquina.
//
//   TavBench fala.wav modelo1.bin [modelo2.bin ...]
//   (TAV_FLASH=0/1 no ambiente liga/desliga o flash attention)
#include "Engine/Segmenter.h"
#include "Engine/TextFilter.h"
#include "Engine/WhisperBackend.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace tav;

static std::vector<float> readWav16k (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    juce::AudioBuffer<float> b (1, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, false);
    return { b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples() };   // o WAV de teste já é 16 kHz
}

int main (int argc, char* argv[])
{
    if (argc < 3) { std::cerr << "uso: TavBench fala.wav modelo.bin [...]\n"; return 2; }
    const auto audio = readWav16k (juce::File (juce::String::fromUTF8 (argv[1])));

    // as falas, separadas como no plugin
    juce::String err;
    auto vad = whisper::Vad::create (err);
    Segmenter seg;
    std::vector<Segmenter::Event> ev;
    const int frames = (int) audio.size() / Segmenter::kFrame;
    std::vector<float> probs ((size_t) frames);
    vad->process (audio.data(), frames, probs.data());
    for (int i = 0; i < frames; ++i) seg.processFrame (audio.data() + (size_t) i * Segmenter::kFrame, probs[(size_t) i], ev);
    seg.flush (ev);
    std::vector<std::vector<float>> phrases;
    double speech = 0;
    for (auto& e : ev) if (e.isFinal && ! e.audio.empty()) { phrases.push_back (e.audio); speech += (double) e.audio.size() / 16000.0; }

    const int phys = juce::SystemStats::getNumPhysicalCpus(), logical = juce::SystemStats::getNumCpus();
    std::cout << "CPU: " << juce::SystemStats::getCpuModel() << " | " << phys << " núcleos / " << logical
              << " threads | AVX2: " << (juce::SystemStats::hasAVX2() ? "sim" : "não")
              << " | flash: " << juce::SystemStats::getEnvironmentVariable ("TAV_FLASH", juce::String::fromUTF8 ("padrão")).toRawUTF8()
              << "\n" << phrases.size() << " falas, " << juce::String (speech, 1) << " s de fala\n\n";

    for (int m = 2; m < argc; ++m)
    {
        const juce::File model (juce::String::fromUTF8 (argv[m]));
        auto s = whisper::Session::create (model, err);
        if (s == nullptr) { std::cout << model.getFileName() << ": " << err << "\n"; continue; }
        s->transcribe (phrases[0], {});   // aquece (aloca buffers)

        std::vector<int> threadOpts { phys };
        if (logical != phys) threadOpts.push_back (logical);
        for (int threads : threadOpts)
            for (int floorCtx : { 1500, 768, 512 })
            {
                whisper::Session::Options o;
                o.threads = threads;
                o.economic = floorCtx < 1500;
                o.audioCtxFloor = floorCtx;
                juce::StringArray texts;
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                double worst = 0;
                for (auto& p : phrases)
                {
                    const auto t1 = juce::Time::getMillisecondCounterHiRes();
                    texts.add (text::clean (s->transcribe (p, o)));
                    worst = juce::jmax (worst, (juce::Time::getMillisecondCounterHiRes() - t1) / 1000.0);
                }
                const double total = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                std::cout << "RESULTADO | " << model.getFileName() << " | threads " << threads
                          << " | janela " << (floorCtx == 1500 ? juce::String ("cheia") : "eco " + juce::String (floorCtx))
                          << " | total " << juce::String (total, 2) << " s | média " << juce::String (total / (double) phrases.size(), 2)
                          << " s/fala | pior " << juce::String (worst, 2) << " s\n";
                for (auto& t : texts) std::cout << "      \"" << t.toRawUTF8() << "\"\n";
            }
        std::cout << "\n";
    }
    return 0;
}
