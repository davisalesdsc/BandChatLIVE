#pragma once

#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/TranscriptView.h"

/** Janela do plugin: cabeçalho, status, medidor, INICIAR/PAUSAR, AVANÇADO,
    transcrição e botões de baixo. Mesma identidade do script do REAPER. */
class TranscricaoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TranscricaoEditor (TranscricaoProcessor&);
    ~TranscricaoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void layoutAdvanced (juce::Rectangle<int> r);
    void setAdvancedVisible (bool show);
    void refreshModelInfo();
    void bindText (juce::TextEditor& ed, const juce::Identifier& pref, const juce::String& def, const juce::String& tip);

    TranscricaoProcessor& proc;
    theme::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // topo
    juce::TextButton popoutBtn { TranscricaoProcessor::u8 ("JANELA ⇱") };
    juce::TextButton mainBtn;
    juce::TextButton advBtn;

    // avançado: tudo dentro de um painel com rolagem, para a transcrição
    // nunca sumir em tela pequena
    struct AdvPanel : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            g.setColour (theme::border);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        }
    };
    AdvPanel advPanel;
    juce::Viewport advView;
    juce::Label kwLabel, modeLabel, langLabel, vocabLabel, corrLabel, nameLabel, modelsLabel;
    juce::TextEditor keywords, vocabulary, corrections, channelName;
    juce::ComboBox mode, language;
    juce::Slider gate, sens, hold;
    juce::Label gateLbl, sensLbl, holdLbl;
    juce::ToggleButton partials { "Parciais" }, writeLog { TranscricaoProcessor::u8 ("Gravar histórico") };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateAtt, sensAtt, holdAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> partialsAtt;
    juce::OwnedArray<juce::TextButton> downloadBtns;
    juce::TextButton openModels { "ABRIR PASTA" }, cancelDl { "CANCELAR" };
    double dlProgress = 0.0;
    juce::ProgressBar dlBar { dlProgress };
    juce::Label modelsInfo;

    // transcrição
    TranscriptView view;

    // baixo
    juce::TextButton copyBtn { "COPIAR" }, clearBtn { "LIMPAR" }, logsBtn { "ABRIR LOGS" };
    juce::Slider fontSize;
    juce::Label fontLbl;
    juce::ToggleButton autoscroll { TranscricaoProcessor::u8 ("Rolagem automática") };
    juce::ToggleButton allChannels { TranscricaoProcessor::u8 ("Todos os canais") };

    // estado desenhado no paint
    juce::Rectangle<int> statusArea, meterArea, messageArea, headerArea;
    juce::String capText, trText, msgText, channelText;
    juce::Colour capColour, trColour, msgColour;
    float meterDb = -100.0f;
    int seenDownloads = -1;
    bool advOpen = false;
    bool constructed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscricaoEditor)
};
