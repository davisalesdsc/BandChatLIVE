#pragma once

#include "Common/GlobalPrefs.h"
#include "Engine/DirectInput.h"
#include "Engine/Resampler16k.h"
#include "Engine/Transcriber.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

class PopoutWindow;

/** O plugin só ESCUTA: o áudio passa intacto. Cada bloco do host é somado em
    mono, filtrado (anti-alias), reamostrado para 16 kHz e entregue ao motor
    por uma fila sem trava. */
class TranscricaoProcessor : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::ValueTree::Listener,
                             private juce::ChangeListener,
                             private juce::AsyncUpdater,
                             private juce::Timer
{
public:
    TranscricaoProcessor();
    ~TranscricaoProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BandChatLIVE"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    /** O host informa o nome/cor da faixa (REAPER, Cubase, Studio One, Live,
        Logic...): vira o nome do canal na conversa, se ninguém digitou outro. */
    void updateTrackProperties (const TrackProperties& properties) override;
    juce::String getTrackName() const { const juce::ScopedLock sl (trackLock); return trackName; }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    tav::Transcriber& getTranscriber() noexcept { return transcriber; }
    juce::AudioProcessorValueTreeState& getParams() noexcept { return params; }

    /** Preferências que não são automatizáveis. As globais (correções,
        vocabulário, palavras, modo, idioma, fonte...) ficam no computador e
        valem para todos os canais, projetos e o app (tav::GlobalPrefs); as
        outras (nome do canal, tamanho da janela) ficam na sessão do host. */
    juce::ValueTree getPrefs() { return params.state.getOrCreateChildWithName ("prefs", nullptr); }
    juce::var  getPref (const juce::Identifier& id, const juce::var& def);
    void       setPref (const juce::Identifier& id, const juce::var& v);

    float getInputLevelDb() const noexcept
    {
        return directActive.load() && direct != nullptr ? direct->getLevelDb() : levelDb.load();
    }
    /** true se o host chamou processBlock há pouco (a faixa está "viva"). */
    bool  isReceivingAudio() const noexcept;

    /** Fonte do áudio: a faixa do DAW (padrão) ou direto da placa de som. */
    bool isDirectMode() const;
    bool isDirectActive() const noexcept     { return directActive.load(); }
    juce::String getDirectError() const      { return directError; }
    juce::String getDirectDescription() const { return direct != nullptr && directActive.load() ? direct->getDescription() : juce::String(); }
    /** Placas e canais para a lista do AVANÇADO (abre um leitor temporário se preciso). */
    juce::StringArray listInputDevices();
    juce::StringArray listInputChannels (const juce::String& device);

    /** O que está chegando do host, para a mensagem certa na tela e no log. */
    enum class AudioStatus { notCalled, noInputChannels, silence, ok };
    AudioStatus getAudioStatus() const noexcept;

    //-- janela pop-out (fica aberta mesmo com a janela do plugin fechada) ----
    void togglePopout();
    bool isPopoutOpen() const noexcept { return popout != nullptr; }
    void closePopout();

    static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

private:
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float value) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void pushSettings();
    void handleAsyncUpdate() override { pushSettings(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { pushSettings(); }
    void timerCallback() override;             // diagnóstico do áudio (log)

    juce::AudioProcessorValueTreeState params;
    tav::Transcriber transcriber;

    // reamostragem da faixa (só na thread de áudio)
    tav::Resampler16k resampler;
    std::vector<float> mono;
    std::atomic<float>* muteParam = nullptr;

    // entrada direta da placa (alternativa à faixa do DAW)
    void applyInputSource();
    void ensureDirect();
    std::unique_ptr<tav::DirectInput> direct;
    std::atomic<bool> directActive { false };
    juce::String directKey, directError;

    std::atomic<float> levelDb { -100.0f };
    std::atomic<juce::int64> lastBlockMs { 0 }, lastSignalMs { 0 };
    std::atomic<int> lastInputChannels { -1 }, lastBlockSize { 0 };
    AudioStatus loggedStatus = AudioStatus::ok;
    bool loggedHost = false;
    std::atomic<float>* transcribeParam = nullptr;

    std::unique_ptr<PopoutWindow> popout;
    std::shared_ptr<tav::GlobalPrefs> globalPrefs;

    juce::CriticalSection trackLock;
    juce::String trackName;
    juce::uint32 trackColour = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscricaoProcessor)
};
