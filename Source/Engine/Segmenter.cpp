#include "Segmenter.h"

#include <algorithm>
#include <cmath>

namespace tav
{

Segmenter::Segmenter() : Segmenter (Settings()) {}
Segmenter::Segmenter (const Settings& s) { setSettings (s); reset(); }

int Segmenter::frames (float seconds) const noexcept
{
    return (int) std::lround (seconds * (float) kRate / (float) kFrame);
}

void Segmenter::setSettings (const Settings& s)
{
    settings      = s;
    endFrames     = std::max (1,  frames ((float) s.holdMs / 1000.0f));
    maxFrames     = std::max (10, frames (s.maxSegmentS));
    minVoiced     = std::max (1,  frames ((float) s.minSpeechMs / 1000.0f));
    partialFrames = std::max (8,  frames ((float) s.partialMs / 1000.0f));
    tailFrames    = frames (0.3f);
    preFrames     = frames (0.5f) + kStartFrames;
}

void Segmenter::reset()
{
    recent.clear();
    noiseInit = false;
    noiseDb = -90.0f;
    speaking = false;
    cur.clear();
    curDb.clear();
    voiced = silence = run = sincePartial = 0;
}

void Segmenter::processFrame (const float* fr, float prob, std::vector<Event>& out)
{
    double sum = 0.0;
    for (int i = 0; i < kFrame; ++i) sum += (double) fr[i] * fr[i];
    const float db = (float) (10.0 * std::log10 (sum / kFrame + 1e-18));
    lastDb = db;

    if (! noiseInit) { noiseDb = std::max (db, -90.0f); noiseInit = true; }
    const float thr = std::max (settings.minDb, noiseDb + settings.noiseMarginDb);
    const int64_t idx = frameIndex++;

    if (db < noiseDb)                                        // cai rápido, sempre
        noiseDb = 0.9f * noiseDb + 0.1f * std::max (db, -90.0f);

    const bool vadVoice = ! settings.useVad || prob >= settings.vadThreshold;

    if (! speaking)
    {
        recent.push_back ({ idx, std::vector<float> (fr, fr + kFrame), db });
        while ((int) recent.size() > preFrames) recent.pop_front();

        // o ruído de fundo sobe devagar, e nunca aprendendo com voz
        if (db >= noiseDb && ! (settings.useVad && vadVoice))
            noiseDb += 0.01f * (db - noiseDb);

        // início: 3 quadros seguidos com nível e "quase voz", e pelo menos um
        // acima do limiar (o VAD oscila no ataque de falas baixas)
        const bool nearVoice = ! settings.useVad || prob >= settings.vadThreshold - 0.15f;
        if (db > thr && nearVoice)
        {
            runPeak = std::max (runPeak, vadVoice ? 1.0f : 0.0f);
            if (++run >= kStartFrames && runPeak > 0.0f)
            {
                speaking = true;
                cur.clear();
                curDb.clear();
                for (auto& p : recent)
                {
                    cur.insert (cur.end(), p.frame.begin(), p.frame.end());
                    curDb.push_back (p.db);
                }
                curStartFrame = recent.front().index;
                voiced = run;
                silence = sincePartial = 0;
            }
        }
        else
        {
            run = 0;
            runPeak = 0.0f;
        }
        return;
    }

    cur.insert (cur.end(), fr, fr + kFrame);
    curDb.push_back (db);
    ++sincePartial;

    // histerese: o fim fraco de uma palavra não fecha a frase
    const bool contVoice = ! settings.useVad || prob >= settings.vadThreshold - 0.15f;
    if (db > thr - 3.0f && contVoice) { ++voiced; silence = 0; }
    else                              { ++silence; }

    if (silence >= endFrames || (int) curDb.size() >= maxFrames)
    {
        emit (out);
    }
    else if (sincePartial >= partialFrames && silence < 3)
    {
        sincePartial = 0;
        Event e;
        e.isFinal = false;
        e.startSample = curStartFrame * kFrame;
        e.audio = cur;
        out.push_back (std::move (e));
    }
}

void Segmenter::emit (std::vector<Event>& out)
{
    int n = (int) curDb.size();
    const int trim = std::max (0, silence - tailFrames);    // mantém 0,3 s de cauda
    n = std::max (0, n - trim);

    bool good;
    if (settings.useVad)
    {
        good = n > 0 && (float) voiced / (float) n >= 0.25f;
    }
    else
    {
        // sem VAD: fala varia de volume; chiado e zumbido constantes não
        std::vector<float> d (curDb.begin(), curDb.begin() + n);
        float dyn = 99.0f;
        if (d.size() >= 5)
        {
            std::sort (d.begin(), d.end());
            dyn = d[(size_t) (0.9 * (double) (d.size() - 1))] - d[(size_t) (0.1 * (double) (d.size() - 1))];
        }
        good = dyn >= 6.0f;
    }

    Event e;
    e.isFinal = true;
    e.startSample = curStartFrame * kFrame;
    if (voiced >= minVoiced && n > 0 && good)
        e.audio.assign (cur.begin(), cur.begin() + (size_t) n * kFrame);
    out.push_back (std::move (e));

    speaking = false;
    cur.clear();
    curDb.clear();
    voiced = silence = run = sincePartial = 0;
    runPeak = 0.0f;
    recent.clear();
}

void Segmenter::flush (std::vector<Event>& out)
{
    if (speaking) emit (out);
}

} // namespace tav
