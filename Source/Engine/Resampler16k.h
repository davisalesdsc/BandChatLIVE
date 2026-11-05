#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace tav
{

/** Leva áudio mono de qualquer taxa para 16 kHz (a do Whisper): dois
    passa-baixas de 2ª ordem em 7 kHz (anti-alias) + interpolação linear.
    Feito para a thread de áudio: depois do prepare() não aloca nada.
    Usado pela faixa do DAW e pela entrada direta da placa. */
class Resampler16k
{
public:
    void prepare (double inputRate, int maxBlock)
    {
        rate  = inputRate > 0 ? inputRate : 48000.0;
        step  = rate / 16000.0;
        phase = 0.0;
        prev  = 0.0f;
        const auto c = juce::IIRCoefficients::makeLowPass (rate, juce::jmin (7000.0, rate * 0.45), 0.707);
        aa1.setCoefficients (c); aa1.reset();
        aa2.setCoefficients (c); aa2.reset();
        work.assign ((size_t) juce::jmax (maxBlock, 64), 0.0f);
        out.assign ((size_t) ((double) work.size() / step) + 16, 0.0f);
    }

    int maxBlock() const noexcept { return (int) work.size(); }

    /** n amostras mono -> devolve quantas saíram em output() (16 kHz). */
    int process (const float* mono, int n) noexcept
    {
        if (n <= 0 || n > (int) work.size()) return 0;
        std::copy (mono, mono + n, work.begin());
        aa1.processSamples (work.data(), n);
        aa2.processSamples (work.data(), n);

        int produced = 0;
        const int cap = (int) out.size();
        while (phase < (double) (n - 1) && produced < cap)
        {
            float v;
            if (phase < 0.0)
            {
                v = prev + (work[0] - prev) * (float) (phase + 1.0);
            }
            else
            {
                const int i = (int) phase;
                const float f = (float) (phase - i);
                v = work[(size_t) i] + (work[(size_t) i + 1] - work[(size_t) i]) * f;
            }
            out[(size_t) produced++] = v;
            phase += step;
        }
        phase -= (double) n;
        prev = work[(size_t) n - 1];
        return produced;
    }

    const float* output() const noexcept { return out.data(); }

private:
    double rate = 48000.0, step = 3.0, phase = 0.0;
    float prev = 0.0f;
    juce::IIRFilter aa1, aa2;
    std::vector<float> work, out;
};

} // namespace tav
