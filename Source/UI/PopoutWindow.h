#pragma once

#include "TranscriptView.h"
#include "Theme.h"

/** Janela separada só com a transcrição (texto grande para o palco / técnica).
    Pertence ao processador: continua aberta mesmo se a janela do plugin fechar. */
class PopoutWindow : public juce::DocumentWindow
{
public:
    explicit PopoutWindow (TranscricaoProcessor& p);
    ~PopoutWindow() override;
    void closeButtonPressed() override;

private:
    class Body : public juce::Component, private juce::Timer
    {
    public:
        Body (TranscricaoProcessor& p, PopoutWindow& w);
        void resized() override;
        void paint (juce::Graphics& g) override;
    private:
        void timerCallback() override;
        TranscricaoProcessor& proc;
        PopoutWindow& window;
        juce::ToggleButton pin { TranscricaoProcessor::u8 ("Sempre na frente") }, autoscroll { TranscricaoProcessor::u8 ("Rolagem") },
                           allChannels { TranscricaoProcessor::u8 ("Todos os canais") };
        juce::Slider fontSize;
        juce::TextButton startStop, copy { "COPIAR" };
        TranscriptView view;
        juce::String status;
        juce::Colour statusColour;
    };

    TranscricaoProcessor& proc;
    theme::LookAndFeel lnf;
};
