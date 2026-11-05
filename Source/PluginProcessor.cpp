#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/TextFilter.h"
#include "Common/TranscriptLog.h"
#include "UI/PopoutWindow.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace ids
{
    static const juce::String transcribe = "transcrever", gate = "gate", vadsens = "vadsens",
                              hold = "fimfrase", partials = "parciais", mute = "silenciar";
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
      params (*this, nullptr, "BandChatLIVE", createLayout())
{
    globalPrefs = tav::GlobalPrefs::get();
    globalPrefs->addChangeListener (this);
    startTimer (1000);
    transcribeParam = params.getRawParameterValue (ids::transcribe);
    muteParam       = params.getRawParameterValue (ids::mute);
    for (auto* id : { &ids::gate, &ids::vadsens, &ids::hold, &ids::partials })
        params.addParameterListener (*id, this);
    params.state.addListener (this);
    pushSettings();
}

TranscricaoProcessor::~TranscricaoProcessor()
{
    stopTimer();
    direct.reset();                         // antes do motor: o callback da placa escreve nele
    cancelPendingUpdate();
    globalPrefs->removeChangeListener (this);
    globalPrefs->flush();
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
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { ids::partials, 1 }, "Parciais", tav::models::partialsByDefault()));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { ids::mute, 1 }, u8 ("Silenciar saída"), false));
    return l;
}

juce::var TranscricaoProcessor::getPref (const juce::Identifier& id, const juce::var& def)
{
    auto p = getPrefs();
    if (tav::GlobalPrefs::isGlobal (id))
    {
        if (globalPrefs->has (id)) return globalPrefs->getValue (id, def);
        // migração: quem já tinha digitado na sessão (versões até 0.5) não perde
        if (p.hasProperty (id)) { globalPrefs->setValue (id, p.getProperty (id)); return p.getProperty (id); }
        return def;
    }
    return p.hasProperty (id) ? p.getProperty (id) : def;
}

void TranscricaoProcessor::setPref (const juce::Identifier& id, const juce::var& v)
{
    if (tav::GlobalPrefs::isGlobal (id))
        globalPrefs->setValue (id, v);       // avisa todos os canais (changeListenerCallback)
    else
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
    {
        // nome digitado no AVANÇADO tem prioridade; senão, o nome da faixa no host
        const juce::ScopedLock sl (trackLock);
        const auto typed = getPref ("canal", "").toString().trim();
        s.channelName   = typed.isNotEmpty() ? typed : trackName;
        s.channelColour = trackColour;
    }
    s.writeLog       = (bool) getPref ("log", true);
    transcriber.setSettings (s);
    applyInputSource();
}

bool TranscricaoProcessor::isDirectMode() const
{
    if (juce::JUCEApplicationBase::isStandaloneApp()) return false;     // o app já lê a placa
    auto& self = const_cast<TranscricaoProcessor&> (*this);
    return self.getPref ("entrada", "daw").toString() == "placa";
}

void TranscricaoProcessor::ensureDirect()
{
    if (direct != nullptr) return;
    direct = std::make_unique<tav::DirectInput> ([this] (const float* x, int n)
    {
        // thread da placa: o mesmo papel do processBlock quando o áudio vem da faixa
        const bool on = transcribeParam->load() > 0.5f;
        transcriber.setActive (on);
        if (on) transcriber.pushAudio (x, n);
    });
}

void TranscricaoProcessor::applyInputSource()
{
    if (! isDirectMode())
    {
        if (direct != nullptr) { direct->close(); directActive.store (false); }
        directKey.clear();
        directError.clear();
        return;
    }
    const auto device  = getPref ("placa", "").toString();
    const int  channel = (int) getPref ("placaCanal", 0);
    const auto key = device + "|" + juce::String (channel);
    if (key == directKey) return;                 // já está assim
    directKey = key;

    ensureDirect();

    directActive.store (false);
    directError = device.isEmpty() ? u8 ("Escolha a placa e a entrada em AVANÇADO.") : direct->open (device, channel);
    directActive.store (directError.isEmpty());
}

juce::StringArray TranscricaoProcessor::listInputDevices()
{
    ensureDirect();
    return direct->getDeviceNames();
}

juce::StringArray TranscricaoProcessor::listInputChannels (const juce::String& device)
{
    listInputDevices();
    return direct->getChannelNames (device);
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
    if (out.isDisabled())                       // só entrada: SÓ no app (nos hosts, entrada = saída)
        return inOk && juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
    return inOk && in == out;
}

void TranscricaoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int maxBlock = juce::jmax (samplesPerBlock, 4096) * 2;     // folga para host que manda bloco maior
    mono.assign ((size_t) maxBlock, 0.0f);
    resampler.prepare (sampleRate, maxBlock);
}

void TranscricaoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chs = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    const auto now = juce::Time::currentTimeMillis();
    lastBlockMs.store (now);                    // o host chamou (mesmo sem canais de entrada)
    lastInputChannels.store (chs);
    lastBlockSize.store (n);
    if (n <= 0 || chs <= 0) return;

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
    if (db > -75.0f) lastSignalMs.store (now);
    const float prev = levelDb.load();
    levelDb.store (db > prev ? db : juce::jmax (db, prev - 0.8f));

    const bool on = transcribeParam->load() > 0.5f;
    transcriber.setActive (on);

    // "Silenciar saída": o plugin continua lendo, mas a faixa fica muda
    // (dá para transcrever sem se ouvir no retorno)
    if (muteParam->load() > 0.5f)
        buffer.clear();

    // com a entrada direta da placa, quem alimenta o motor é ela, não a faixa
    if (! on || directActive.load()) return;

    const int produced = resampler.process (mono.data(), n);
    transcriber.pushAudio (resampler.output(), produced);
}

void TranscricaoProcessor::updateTrackProperties (const TrackProperties& p)
{
    {
        const juce::ScopedLock sl (trackLock);
        // alguns hosts (feitos com JUCE, no Mac) entregam o nome em MacRoman
        trackName = tav::text::repairMisdecodedUtf8 (p.name.value_or (juce::String()).trim());
        trackColour = 0;
        if (p.colourARGB.has_value())
        {
            // cor da faixa só se der para ler sobre o fundo preto
            const juce::Colour c (*p.colourARGB);
            if (c.getAlpha() > 0 && c.getPerceivedBrightness() > 0.25f)
                trackColour = c.withAlpha (1.0f).getARGB();
        }
    }
    triggerAsyncUpdate();       // pode chegar fora da thread de mensagens
}

bool TranscricaoProcessor::isReceivingAudio() const noexcept
{
    return getAudioStatus() != AudioStatus::notCalled && getAudioStatus() != AudioStatus::noInputChannels;
}

TranscricaoProcessor::AudioStatus TranscricaoProcessor::getAudioStatus() const noexcept
{
    const auto now = juce::Time::currentTimeMillis();
    if (directActive.load() && direct != nullptr)
    {
        if (now - direct->getLastCallbackMs() > 1500) return AudioStatus::notCalled;
        if (now - direct->getLastSignalMs() > 8000)   return AudioStatus::silence;
        return AudioStatus::ok;
    }
    if (now - lastBlockMs.load() > 1500)  return AudioStatus::notCalled;
    if (lastInputChannels.load() <= 0)    return AudioStatus::noInputChannels;
    if (now - lastSignalMs.load() > 8000) return AudioStatus::silence;
    return AudioStatus::ok;
}

void TranscricaoProcessor::timerCallback()
{
    // registra no log o que o host está fazendo, só quando muda (para suporte)
    const auto st = getAudioStatus();
    if (! loggedHost)
    {
        loggedHost = true;
        tav::debugLog ("audio", "host: " + juce::String (juce::PluginHostType().getHostDescription())
                       + " | formato: " + juce::AudioProcessor::getWrapperTypeDescription (wrapperType)
                       + " | entradas: " + juce::String (getTotalNumInputChannels())
                       + " | saidas: " + juce::String (getTotalNumOutputChannels())
                       + " | " + juce::String (getSampleRate(), 0) + " Hz");
    }
    if (st != loggedStatus)
    {
        loggedStatus = st;
        const char* what = st == AudioStatus::notCalled       ? "o host NAO esta chamando o plugin (faixa sem processamento)"
                         : st == AudioStatus::noInputChannels ? "o host chama o plugin, mas SEM canais de entrada"
                         : st == AudioStatus::silence         ? "chamando, mas so silencio na entrada"
                                                              : "audio chegando";
        tav::debugLog ("audio", juce::String (what) + " | canais: " + juce::String (lastInputChannels.load())
                       + " | bloco: " + juce::String (lastBlockSize.load()));
    }
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
