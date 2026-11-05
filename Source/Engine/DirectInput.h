#pragma once

#include "Resampler16k.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>

namespace tav
{

/** Entrada direta da placa de som, sem passar pelo DAW.

    O plugin abre a entrada da interface por conta própria (como o app faz) e
    escuta UM canal escolhido. Não depende de o DAW processar a faixa: nem
    monitoramento, nem gravação armada. No macOS vários programas podem ler a
    mesma placa ao mesmo tempo; no Windows com ASIO o DAW costuma ocupá-la
    sozinho, e aí open() falha com uma mensagem clara. */
class DirectInput : private juce::AudioIODeviceCallback
{
public:
    /** Recebe áudio mono a 16 kHz (na thread da placa: não pode bloquear). */
    explicit DirectInput (std::function<void (const float*, int)> sink);
    ~DirectInput() override;

    /** Lista "Placa" (nome do dispositivo) das entradas disponíveis. */
    juce::StringArray getDeviceNames();
    juce::StringArray getChannelNames (const juce::String& device);

    /** Abre o dispositivo e escuta o canal (0 = primeiro). Erro: texto. */
    juce::String open (const juce::String& device, int channel);
    void close();

    bool  isOpen() const noexcept            { return opened.load(); }
    float getLevelDb() const noexcept        { return levelDb.load(); }
    juce::int64 getLastCallbackMs() const noexcept { return lastCallbackMs.load(); }
    juce::int64 getLastSignalMs() const noexcept   { return lastSignalMs.load(); }
    juce::String getDescription() const;

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}

    std::function<void (const float*, int)> sink;
    juce::AudioDeviceManager manager;
    Resampler16k resampler;
    int wantedChannel = 0;
    std::atomic<bool> opened { false };
    std::atomic<float> levelDb { -100.0f };
    std::atomic<juce::int64> lastCallbackMs { 0 }, lastSignalMs { 0 };
    juce::String deviceName;
};

} // namespace tav
