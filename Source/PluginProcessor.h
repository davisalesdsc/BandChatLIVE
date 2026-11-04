#pragma once

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
                             private juce::AsyncUpdater
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

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    tav::Transcriber& getTranscriber() noexcept { return transcriber; }
    juce::AudioProcessorValueTreeState& getParams() noexcept { return params; }

    /** Preferências que não são automatizáveis (texto, tamanho da fonte...).
        Ficam salvas na sessão do host junto com os parâmetros. */
    juce::ValueTree getPrefs() { return params.state.getOrCreateChildWithName ("prefs", nullptr); }
    juce::var  getPref (const juce::Identifier& id, const juce::var& def);
    void       setPref (const juce::Identifier& id, const juce::var& v);

    float getInputLevelDb() const noexcept { return levelDb.load(); }
    /** true se o host chamou processBlock há pouco (a faixa está "viva"). */
    bool  isReceivingAudio() const noexcept;

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

    juce::AudioProcessorValueTreeState params;
    tav::Transcriber transcriber;

    // reamostragem (só na thread de áudio)
    double inRate = 48000.0, step = 3.0, phase = 0.0;
    float  prevSample = 0.0f;
    juce::IIRFilter aa1, aa2;
    std::vector<float> mono, out16;

    std::atomic<float> levelDb { -100.0f };
    std::atomic<juce::int64> lastBlockMs { 0 };
    std::atomic<float>* transcribeParam = nullptr;

    std::unique_ptr<PopoutWindow> popout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscricaoProcessor)
};
