#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/PopoutWindow.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace ids
{
    static const juce::String transcribe = "transcrever", gate = "gate", vadsens = "vadsens",
                              hold = "fimfrase", partials = "parciais";
}

/** No app Standalone o plugin só ESCUTA o microfone: sem saída, não há risco
    de microfonia e o JUCE não silencia a entrada. Nos hosts, entrada = saída
    (o áudio passa intacto pelo canal). */
juce::AudioProcessor::BusesProperties TranscricaoProcessor::makeBuses()
{
    if (juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone)
        return juce::AudioProcessor::BusesProperties().withInput ("Entrada", juce::AudioChannelSet::stereo(), true);
    return juce::AudioProcessor::BusesProperties()
             .withInput  ("Entrada", juce::AudioChannelSet::stereo(), true)
             .withOutput ("Saida",   juce::AudioChannelSet::stereo(), true);
}

TranscricaoProcessor::TranscricaoProcessor()
    : AudioProcessor (makeBuses()),
      params (*this, nullptr, "TranscricaoAoVivo", createLayout())
{
    transcribeParam = params.getRawParameterValue (ids::transcribe);
    for (auto* id : { &ids::gate, &ids::vadsens, &ids::hold, &ids::partials })
        params.addParameterListener (*id, this);
    params.state.addListener (this);
    pushSettings();
}

TranscricaoProcessor::~TranscricaoProcessor()
{
    cancelPendingUpdate();
    params.state.removeListener (this);
    closePopout();
}

APVTS::ParameterLayout TranscricaoProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { ids::transcribe, 1 }, u8 ("Transcrever"), true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::gate, 1 }, "Gate",
              NormalisableRange<float> (-80.0f, -10.0f, 0.5f), -48.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::vadsens, 1 }, u8 ("Sensibilidade"),
              NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::hold, 1 }, "Fim de frase",
              NormalisableRange<float> (300.0f, 2000.0f, 10.0f), 700.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { ids::partials, 1 }, "Parciais", true));
    return l;
}

juce::var TranscricaoProcessor::getPref (const juce::Identifier& id, const juce::var& def)
{
    auto p = getPrefs();
    return p.hasProperty (id) ? p.getProperty (id) : def;
}

void TranscricaoProcessor::setPref (const juce::Identifier& id, const juce::var& v)
{
    getPrefs().setProperty (id, v, nullptr);
}

void TranscricaoProcessor::pushSettings()
{
    tav::Transcriber::Settings s;
    s.gateDb         = params.getRawParameterValue (ids::gate)->load();
    s.vadSensitivity = params.getRawParameterValue (ids::vadsens)->load();
    s.holdMs         = (int) params.getRawParameterValue (ids::hold)->load();
    s.partials       = params.getRawParameterValue (ids::partials)->load() > 0.5f;
    s.language       = getPref ("idioma", "pt").toString();
    s.mode           = (tav::models::Mode) (int) getPref ("modo", 0);
    s.vocabulary     = getPref ("vocabulario", u8 ("bumbo, caixa, retorno, fone, in-ear, guitarra, violão, baixo, teclado, voz, monitor, volume")).toString();
    s.corrections    = getPref ("correcoes", "").toString();
    s.channelName    = getPref ("canal", "").toString();
    s.writeLog       = (bool) getPref ("log", true);
    transcriber.setSettings (s);
}

void TranscricaoProcessor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();     // pode vir da thread de áudio (automação): só agenda
}

void TranscricaoProcessor::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&)
{
    if (t.hasType ("prefs")) pushSettings();
}

//==============================================================================
bool TranscricaoProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    if (out.isDisabled()) return inOk;          // Standalone: só entrada
    return inOk && in == out;
}

void TranscricaoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    inRate = sampleRate > 0 ? sampleRate : 48000.0;
    step = inRate / 16000.0;
    phase = 0.0;
    prevSample = 0.0f;
    // anti-alias: 2 passa-baixas de 2ª ordem em 7 kHz antes de descer para 16 kHz
    const auto c = juce::IIRCoefficients::makeLowPass (inRate, juce::jmin (7000.0, inRate * 0.45), 0.707);
    aa1.setCoefficients (c); aa1.reset();
    aa2.setCoefficients (c); aa2.reset();
    const int maxBlock = juce::jmax (samplesPerBlock, 4096) * 2;     // folga para host que manda bloco maior
    mono.assign ((size_t) maxBlock, 0.0f);
    out16.assign ((size_t) (maxBlock / step) + 16, 0.0f);
}

void TranscricaoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chs = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    if (n <= 0 || chs <= 0) return;
    lastBlockMs.store (juce::Time::currentTimeMillis());

    // o áudio passa intacto: só lemos o buffer
    if (n > (int) mono.size()) return;          // bloco maior que o anunciado: não aloca aqui

    juce::FloatVectorOperations::copy (mono.data(), buffer.getReadPointer (0), n);
    for (int ch = 1; ch < chs; ++ch)
        juce::FloatVectorOperations::add (mono.data(), buffer.getReadPointer (ch), n);
    if (chs > 1) juce::FloatVectorOperations::multiply (mono.data(), 1.0f / (float) chs, n);

    // medidor de pico com queda suave
    const auto range = juce::FloatVectorOperations::findMinAndMax (mono.data(), n);
    const float peak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
    const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
    const float prev = levelDb.load();
    levelDb.store (db > prev ? db : juce::jmax (db, prev - 0.8f));

    const bool on = transcribeParam->load() > 0.5f;
    transcriber.setActive (on);
    if (! on) return;

    aa1.processSamples (mono.data(), n);
    aa2.processSamples (mono.data(), n);

    // reamostragem linear (o sinal já está limitado a 7 kHz)
    int produced = 0;
    const int cap = (int) out16.size();
    while (phase < (double) (n - 1) && produced < cap)
    {
        float v;
        if (phase < 0.0)
        {
            const float f = (float) (phase + 1.0);
            v = prevSample + (mono[0] - prevSample) * f;
        }
        else
        {
            const int i = (int) phase;
            const float f = (float) (phase - i);
            v = mono[(size_t) i] + (mono[(size_t) i + 1] - mono[(size_t) i]) * f;
        }
        out16[(size_t) produced++] = v;
        phase += step;
    }
    phase -= (double) n;
    prevSample = mono[(size_t) n - 1];

    transcriber.pushAudio (out16.data(), produced);
}

bool TranscricaoProcessor::isReceivingAudio() const noexcept
{
    return juce::Time::currentTimeMillis() - lastBlockMs.load() < 1500;
}

//==============================================================================
void TranscricaoProcessor::togglePopout()
{
    if (popout != nullptr) { closePopout(); return; }
    popout = std::make_unique<PopoutWindow> (*this);
}

void TranscricaoProcessor::closePopout()
{
    popout.reset();
}

juce::AudioProcessorEditor* TranscricaoProcessor::createEditor()
{
    return new TranscricaoEditor (*this);
}

void TranscricaoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TranscricaoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (params.state.getType()))
        {
            params.replaceState (juce::ValueTree::fromXml (*xml));
            params.state.addListener (this);
            pushSettings();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranscricaoProcessor();
}
