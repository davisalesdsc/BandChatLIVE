#include "DirectInput.h"
#include "../Common/TranscriptLog.h"

namespace tav
{

DirectInput::DirectInput (std::function<void (const float*, int)> s) : sink (std::move (s)) {}

DirectInput::~DirectInput()
{
    close();
}

juce::StringArray DirectInput::getDeviceNames()
{
    juce::StringArray names;
    for (auto* type : manager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        names.addArray (type->getDeviceNames (true));     // true = entradas
    }
    names.removeDuplicates (false);
    return names;
}

juce::StringArray DirectInput::getChannelNames (const juce::String& device)
{
    for (auto* type : manager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        if (type->getDeviceNames (true).contains (device))
            if (std::unique_ptr<juce::AudioIODevice> d (type->createDevice ({}, device)); d != nullptr)
                return d->getInputChannelNames();
    }
    return {};
}

juce::String DirectInput::open (const juce::String& device, int channel)
{
    close();
    if (device.isEmpty()) return juce::String::fromUTF8 ("Escolha a placa de som.");

    for (auto* type : manager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        if (! type->getDeviceNames (true).contains (device)) continue;

        manager.setCurrentAudioDeviceType (type->getTypeName(), false);
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        manager.getAudioDeviceSetup (setup);
        setup.inputDeviceName  = device;
        setup.outputDeviceName = {};              // só entrada: nunca toca nada
        setup.useDefaultInputChannels  = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setBit (juce::jmax (0, channel));
        setup.outputChannels.clear();

        const auto err = manager.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty() || manager.getCurrentAudioDevice() == nullptr)
        {
            const auto msg = juce::String::fromUTF8 ("Não consegui abrir a placa \"") + device + "\": "
                           + (err.isNotEmpty() ? err : juce::String::fromUTF8 ("ocupada por outro programa?"));
            debugLog ("placa", msg);
            manager.closeAudioDevice();
            return msg;
        }
        wantedChannel = juce::jmax (0, channel);
        deviceName = device;
        manager.addAudioCallback (this);
        opened.store (true);
        debugLog ("placa", "entrada direta aberta: " + getDescription());
        return {};
    }
    return juce::String::fromUTF8 ("Placa não encontrada: ") + device;
}

void DirectInput::close()
{
    if (! opened.exchange (false)) return;
    manager.removeAudioCallback (this);
    manager.closeAudioDevice();
    debugLog ("placa", "entrada direta fechada");
}

juce::String DirectInput::getDescription() const
{
    if (auto* d = manager.getCurrentAudioDevice())
        return d->getName() + " | entrada " + juce::String (wantedChannel + 1) + " | "
             + juce::String (d->getCurrentSampleRate(), 0) + " Hz";
    return {};
}

void DirectInput::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    resampler.prepare (d->getCurrentSampleRate(), juce::jmax (4096, d->getCurrentBufferSizeSamples() * 2));
}

void DirectInput::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], numSamples);

    const auto now = juce::Time::currentTimeMillis();
    lastCallbackMs.store (now);
    // só o canal escolhido fica ativo: ele chega como o primeiro (e único) canal
    if (numIn < 1 || in[0] == nullptr || numSamples <= 0) return;

    const auto range = juce::FloatVectorOperations::findMinAndMax (in[0], numSamples);
    const float peak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
    const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
    const float prevDb = levelDb.load();
    levelDb.store (db > prevDb ? db : juce::jmax (db, prevDb - 0.8f));
    if (db > -75.0f) lastSignalMs.store (now);

    for (int done = 0; done < numSamples; )
    {
        const int n = juce::jmin (numSamples - done, resampler.maxBlock());
        const int produced = resampler.process (in[0] + done, n);
        if (produced > 0 && sink) sink (resampler.output(), produced);
        done += n;
    }
}

} // namespace tav
