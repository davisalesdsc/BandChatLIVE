#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

/** Lista de falas com horário, quebra de linha, palavras-chave em vermelho,
    parciais apagadas e rolagem automática. Lê as linhas do motor por timer. */
class TranscriptView : public juce::Component, private juce::Timer
{
public:
    explicit TranscriptView (TranscricaoProcessor& p);
    ~TranscriptView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Texto das frases finais, "[HH:MM:SS] texto" por linha. */
    juce::String finalTextForCopy() const;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (TranscriptView& o) : owner (o) {}
        void paint (juce::Graphics& g) override;
        TranscriptView& owner;
    };

    struct Row
    {
        juce::String time;
        juce::TextLayout layout;
        juce::Colour timeColour;
        float y = 0, h = 0;
    };

    void timerCallback() override;
    void rebuild();

    TranscricaoProcessor& proc;
    juce::Viewport viewport;
    Content content { *this };

    std::vector<tav::Transcriber::Line> lines;
    std::vector<Row> rows;
    int seenVersion = -1, builtWidth = -1, builtFont = -1;
    juce::String builtKeywords;
    bool followBottom = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriptView)
};
