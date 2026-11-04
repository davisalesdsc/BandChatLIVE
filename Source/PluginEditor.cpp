#include "PluginEditor.h"
#include "Engine/Models.h"

static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
static constexpr int kHeaderH = 64;
static constexpr int kAdvH = 372;        // altura do conteúdo do painel avançado
static constexpr int kMinTranscriptH = 150;

TranscricaoEditor::TranscricaoEditor (TranscricaoProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p)
{
    setLookAndFeel (&lnf);

    popoutBtn.setTooltip (u8 ("Abre a transcrição numa janela separada, que pode ficar sempre na frente "
                              "e continua aberta mesmo fechando esta."));
    popoutBtn.onClick = [this] { proc.togglePopout(); };
    addAndMakeVisible (popoutBtn);

    mainBtn.onClick = [this]
    {
        auto* prm = proc.getParams().getParameter ("transcrever");
        prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
    };
    addAndMakeVisible (mainBtn);

    advBtn.onClick = [this] { setAdvancedVisible (! advOpen); };
    addAndMakeVisible (advBtn);

    // ---- avançado --------------------------------------------------------
    auto label = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (theme::font (13.0f));
        l.setColour (juce::Label::textColourId, theme::textDim);
        advPanel.addAndMakeVisible (l);
    };
    label (kwLabel,    u8 ("Destacar palavras"));
    label (modeLabel,  u8 ("Modo"));
    label (langLabel,  u8 ("Idioma"));
    label (vocabLabel, u8 ("Vocabulário"));
    label (corrLabel,  u8 ("Correções"));
    label (nameLabel,  u8 ("Nome do canal"));
    label (modelsLabel, u8 ("Modelos de voz"));

    bindText (keywords, "palavras", u8 ("fone, volume, abaixa, aumenta, retorno, monitor, voz"),
              u8 ("Frases com estas palavras (separadas por vírgula) aparecem em vermelho."));
    bindText (vocabulary, "vocabulario", u8 ("bumbo, caixa, retorno, fone, in-ear, guitarra, violão, baixo, teclado, voz, monitor, volume"),
              u8 ("Nomes e termos que ajudam o reconhecimento (nomes da banda, instrumentos). "
                  "Mantenha curto: lista longa deixa lento e faz o Whisper inventar palavras."));
    bindText (corrections, "correcoes", "",
              u8 ("Conserta palavras no texto final.  Ex.:  david => Davi ; in ear => in-ear"));
    bindText (channelName, "canal", "",
              u8 ("Nome que aparece antes das falas deste canal (ex.: Comunicação Davi). Vazio = usa o nome "
                  "da faixa no DAW, quando o programa informa."));

    mode.addItem (u8 ("Automático"), 1);
    mode.addItem (u8 ("Leve (responde rápido)"), 2);
    mode.addItem (u8 ("Preciso (nunca rebaixa)"), 3);
    mode.setSelectedId ((int) proc.getPref ("modo", 0) + 1, juce::dontSendNotification);
    mode.setTooltip (u8 ("Automático: começa no melhor modelo que esta máquina aguenta e alivia sozinho se atrasar.\n"
                         "Leve: o modelo mais leve instalado.\nPreciso: o mais pesado, mesmo que atrase."));
    mode.onChange = [this] { proc.setPref ("modo", mode.getSelectedId() - 1); };
    advPanel.addAndMakeVisible (mode);

    const char* langs[][2] = { { "pt", "Português" }, { "en", "English" }, { "es", "Español" }, { "auto", "Detectar" } };
    for (int i = 0; i < 4; ++i) language.addItem (u8 (langs[i][1]), i + 1);
    const auto curLang = proc.getPref ("idioma", "pt").toString();
    for (int i = 0; i < 4; ++i) if (curLang == langs[i][0]) language.setSelectedId (i + 1, juce::dontSendNotification);
    language.onChange = [this, langs] { proc.setPref ("idioma", juce::String (langs[language.getSelectedId() - 1][0])); };
    advPanel.addAndMakeVisible (language);

    auto knob = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        s.setTooltip (tip);
        advPanel.addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (theme::font (12.0f, true));
        l.setColour (juce::Label::textColourId, theme::textDim);
        advPanel.addAndMakeVisible (l);
    };
    knob (gate, gateLbl, "GATE", u8 ("Nível mínimo de fala (linha branca no medidor). Deixe um pouco acima do vazamento do palco."));
    knob (sens, sensLbl, "SENSIBILIDADE", u8 ("Detector de voz: mais alto pega fala mais baixa, mas deixa passar mais coisa."));
    knob (hold, holdLbl, "FIM DE FRASE", u8 ("Silêncio que fecha a frase. Quem fala pausado: aumente."));
    auto& ap = proc.getParams();
    gateAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, "gate", gate);
    sensAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, "vadsens", sens);
    holdAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, "fimfrase", hold);
    partialsAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ap, "parciais", partials);
    partials.setTooltip (u8 ("Mostra o texto enquanto a pessoa ainda fala. Desligue em computador fraco."));
    advPanel.addAndMakeVisible (partials);
    writeLog.setToggleState ((bool) proc.getPref ("log", true), juce::dontSendNotification);
    writeLog.onClick = [this] { proc.setPref ("log", writeLog.getToggleState()); };
    writeLog.setTooltip (u8 ("Grava as frases num .txt por dia (botão ABRIR LOGS)."));
    advPanel.addAndMakeVisible (writeLog);

    int n = 0;
    const auto* cat = tav::models::catalogue (n);
    for (int i = 0; i < n; ++i)
    {
        auto* b = downloadBtns.add (new juce::TextButton (u8 ("Baixar ") + u8 (cat[i].label) + "\n" + cat[i].size));
        b->setTooltip (u8 ("Baixa o modelo ") + cat[i].file + u8 (" (uma vez só; depois funciona offline)."));
        b->onClick = [this, d = cat[i]]
        {
            if (! tav::models::Downloader::get().start (d))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, u8 ("Download"),
                    u8 ("Já existe um download em andamento ou não há conexão."));
        };
        advPanel.addAndMakeVisible (b);
    }
    openModels.onClick = [] { tav::models::modelsDir().startAsProcess(); };
    openModels.setTooltip (u8 ("Abre a pasta dos modelos. Também aceita arquivos ggml-*.bin baixados à mão."));
    cancelDl.onClick = [] { tav::models::Downloader::get().cancel(); };
    advPanel.addAndMakeVisible (openModels);
    advPanel.addChildComponent (cancelDl);
    advPanel.addChildComponent (dlBar);
    modelsInfo.setFont (theme::font (12.0f));
    modelsInfo.setColour (juce::Label::textColourId, theme::textMuted);
    advPanel.addAndMakeVisible (modelsInfo);
    advView.setViewedComponent (&advPanel, false);
    advView.setScrollBarsShown (true, false);
    advView.setScrollBarThickness (8);
    addChildComponent (advView);

    // ---- transcrição e rodapé ------------------------------------------------
    addAndMakeVisible (view);

    copyBtn.onClick  = [this] { juce::SystemClipboard::copyTextToClipboard (view.finalTextForCopy()); };
    clearBtn.onClick = [this] { proc.getTranscriber().getHub().clear(); };
    clearBtn.setTooltip (u8 ("Limpa a conversa de todos os canais."));
    logsBtn.onClick  = [] { tav::models::logsDir().startAsProcess(); };
    logsBtn.setTooltip (u8 ("Abre a pasta com o histórico diário (um .txt por dia)."));
    for (auto* b : { &copyBtn, &clearBtn, &logsBtn }) addAndMakeVisible (b);

    fontSize.setSliderStyle (juce::Slider::LinearHorizontal);
    fontSize.setTextBoxStyle (juce::Slider::TextBoxRight, false, 24, 18);
    fontSize.setRange (1, 5, 1);
    fontSize.setValue ((int) proc.getPref ("fonte", 3), juce::dontSendNotification);
    fontSize.onValueChange = [this] { proc.setPref ("fonte", (int) fontSize.getValue()); };
    addAndMakeVisible (fontSize);
    fontLbl.setText ("Tamanho do texto", juce::dontSendNotification);
    fontLbl.setFont (theme::font (13.0f));
    fontLbl.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (fontLbl);
    autoscroll.setToggleState ((bool) proc.getPref ("autoscroll", true), juce::dontSendNotification);
    autoscroll.onClick = [this] { proc.setPref ("autoscroll", autoscroll.getToggleState()); };
    addAndMakeVisible (autoscroll);
    allChannels.setToggleState ((bool) proc.getPref ("todos", true), juce::dontSendNotification);
    allChannels.onClick = [this] { proc.setPref ("todos", allChannels.getToggleState()); };
    allChannels.setTooltip (u8 ("Ligado: mostra a conversa de todos os BandChatLIVE abertos neste programa. "
                                "Desligado: só deste canal."));
    addAndMakeVisible (allChannels);

    advOpen = (bool) proc.getPref ("avancado", false);
    setResizable (true, true);
    setResizeLimits (480, 520, 2400, 2400);
    setSize ((int) proc.getPref ("largura", 580), (int) proc.getPref ("altura", 720));
    setAdvancedVisible (advOpen);
    refreshModelInfo();
    constructed = true;
    timerCallback();
    startTimerHz (20);
}

TranscricaoEditor::~TranscricaoEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TranscricaoEditor::bindText (juce::TextEditor& ed, const juce::Identifier& pref, const juce::String& def, const juce::String& tip)
{
    ed.setText (proc.getPref (pref, def).toString(), juce::dontSendNotification);
    ed.setCaretPosition (0);
    ed.setFont (theme::font (14.0f));
    ed.setTooltip (tip);
    ed.setIndents (6, 5);
    ed.onTextChange = [this, &ed, pref] { proc.setPref (pref, ed.getText()); };
    advPanel.addAndMakeVisible (ed);
}

void TranscricaoEditor::setAdvancedVisible (bool show)
{
    advOpen = show;
    proc.setPref ("avancado", show);
    advBtn.setButtonText (show ? u8 ("AVANÇADO  ▲") : u8 ("AVANÇADO  ▼"));
    advView.setVisible (show);
    resized();
    repaint();
}

void TranscricaoEditor::refreshModelInfo()
{
    juce::StringArray names;
    for (auto& f : tav::models::installed()) names.add (tav::models::label (f));
    modelsInfo.setText (names.isEmpty() ? u8 ("Nenhum modelo instalado: baixe um abaixo (precisa de internet só agora).")
                                        : u8 ("Instalados: ") + names.joinIntoString (", "),
                        juce::dontSendNotification);
}

//==============================================================================
void TranscricaoEditor::timerCallback()
{
    auto& tr = proc.getTranscriber();
    const bool on = proc.getParams().getRawParameterValue ("transcrever")->load() > 0.5f;

    mainBtn.setButtonText (on ? u8 ("PAUSAR TRANSCRIÇÃO") : u8 ("INICIAR"));
    mainBtn.getProperties().set ("primary", ! on);
    mainBtn.repaint();

    if (! on)                          { capText = u8 ("PAUSADO");           capColour = theme::textMuted; }
    else if (! proc.isReceivingAudio()) { capText = u8 ("SEM ÁUDIO");         capColour = theme::warn; }
    else if (tr.isSpeaking())          { capText = u8 ("FALA DETECTADA");    capColour = theme::accentHv; }
    else                               { capText = u8 ("CAPTANDO");          capColour = theme::ok; }

    using S = tav::Transcriber::State;
    const auto st = tr.getState();
    const auto model = tr.getModelLabel();
    const juce::String extras = (tr.isVadActive() ? " + VAD" : "") + juce::String (tr.isUsingGpu() ? " + GPU" : "");
    switch (st)
    {
        case S::noModel:      trText = u8 ("SEM MODELO DE VOZ");                     trColour = theme::warn;  break;
        case S::loading:      trText = u8 ("CARREGANDO ") + model.toUpperCase() + "..."; trColour = theme::warn;  break;
        case S::error:        trText = u8 ("ERRO NO MODELO");                        trColour = theme::error; break;
        case S::transcribing:
        case S::listening:    trText = model.toUpperCase() + extras;                 trColour = theme::ok;    break;
    }

    msgText = tr.getMessage();
    msgColour = st == S::error ? theme::error : theme::warn;
    if (msgText.isEmpty() && on && ! proc.isReceivingAudio())
        msgText = juce::JUCEApplicationBase::isStandaloneApp()
                    ? u8 ("Nenhum áudio chegando: escolha o microfone em Options > Audio Settings.")
                    : u8 ("O host não está mandando áudio: ligue o monitoramento de entrada da faixa.");
    if (msgText.isEmpty() && tr.getDelaySeconds() > 6.0f)
        msgText = u8 ("Transcrição atrasada (") + juce::String (tr.getDelaySeconds(), 0) + u8 (" s). O plugin vai aliviar sozinho.");

    meterDb = proc.getInputLevelDb();

    const auto chName = tr.getChannelName();
    const int nCh = tr.getHub().getNumSources();
    channelText = chName.isNotEmpty() ? "Canal: " + chName : juce::String();
    if (nCh > 1) channelText << "  (" << nCh << " canais abertos)";
    const auto track = proc.getTrackName();
    channelName.setTextToShowWhenEmpty (track.isNotEmpty() ? u8 ("automático: ") + track
                                                           : u8 ("automático: nome da faixa no DAW"),
                                        theme::textMuted);

    // downloads
    auto& dl = tav::models::Downloader::get();
    const bool busy = dl.isBusy();
    dlProgress = busy ? (double) dl.getProgress() : 0.0;
    dlBar.setTextToDisplay (busy ? u8 ("Baixando ") + dl.getCurrentLabel() + "..." : juce::String());
    dlBar.setVisible (busy);
    cancelDl.setVisible (busy);
    for (auto* b : downloadBtns) b->setEnabled (! busy);
    if (dl.getFinishedCount() != seenDownloads)
    {
        if (seenDownloads >= 0) tr.refreshModel();   // modelo novo: o modo escolhe de novo
        seenDownloads = dl.getFinishedCount();
        refreshModelInfo();
    }
    if (! busy && dl.getLastResult().startsWith ("erro"))
        modelsInfo.setText (dl.getLastResult(), juce::dontSendNotification);

    repaint (statusArea.getUnion (meterArea).getUnion (messageArea).getUnion (headerArea));
}

//==============================================================================
void TranscricaoEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    // faixa vermelha na borda esquerda
    g.setColour (theme::accent.withAlpha (0.4f));
    g.fillRect (0, 0, 3, getHeight());
    g.setColour (theme::accentHv);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    // cabeçalho
    auto h = headerArea;
    g.setFont (theme::font (11.0f));
    g.setColour (theme::textDim);
    g.drawText ("Desenvolvido por", h.getX(), h.getY() + 2, 200, 14, juce::Justification::left);
    g.drawText ("Davi Sales", h.getX(), h.getY() + 15, 200, 14, juce::Justification::left);
    auto title = h.withTrimmedTop (30);
    g.setFont (theme::font (24.0f, true));
    g.setColour (theme::text);
    const auto t1 = juce::String ("BANDCHAT");
    g.drawText (t1, title, juce::Justification::left);
    g.setColour (theme::accentHv);
    g.drawText ("LIVE", title.withTrimmedLeft ((int) juce::GlyphArrangement::getStringWidth (theme::font (24.0f, true), t1)),
                juce::Justification::left);
    g.setFont (theme::font (11.0f));
    g.setColour (theme::textMuted);
    if (channelText.isNotEmpty())
    {
        g.setFont (theme::font (12.0f, true));
        g.setColour (theme::textDim);
        g.drawText (channelText, h.withTrimmedTop (34).withTrimmedRight (popoutBtn.getWidth() + 10).removeFromTop (14),
                    juce::Justification::right, true);
        g.setFont (theme::font (11.0f));
        g.setColour (theme::textMuted);
    }
    g.drawText (juce::String ("v") + TAV_VERSAO, h.withTrimmedRight (popoutBtn.getWidth() + 10).withTrimmedTop (48),
                juce::Justification::right);

    // status: dois pontos coloridos
    auto s = statusArea;
    auto dot = [&g] (juce::Rectangle<int> r, juce::Colour c, const juce::String& txt)
    {
        g.setColour (c);
        g.fillEllipse ((float) r.getX(), (float) r.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.0f, true));
        g.drawText (txt, r.withTrimmedLeft (16), juce::Justification::centredLeft, true);
    };
    dot (s.removeFromLeft (s.getWidth() / 2 - 10), capColour, capText);
    dot (s.withTrimmedLeft (10), trColour, trText);

    // medidor com a linha do gate
    auto m = meterArea.toFloat();
    g.setColour (theme::panelAlt);
    g.fillRoundedRectangle (m, 3.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (meterDb + 70.0f) / 70.0f);
    if (frac > 0.01f)
    {
        g.setColour (frac > 0.97f ? theme::error : (frac > 0.85f ? theme::warn : theme::accent));
        g.fillRoundedRectangle (m.withWidth (m.getWidth() * frac), 3.0f);
    }
    const float gateDb = proc.getParams().getRawParameterValue ("gate")->load();
    const float gx = m.getX() + m.getWidth() * juce::jlimit (0.0f, 1.0f, (gateDb + 70.0f) / 70.0f);
    g.setColour (theme::text.withAlpha (0.8f));
    g.fillRect (gx - 1.0f, m.getY() - 2.0f, 2.0f, m.getHeight() + 4.0f);

    // mensagem
    if (msgText.isNotEmpty())
    {
        g.setColour (msgColour);
        g.setFont (theme::font (12.5f));
        g.drawFittedText (msgText, messageArea, juce::Justification::centredLeft, 2);
    }

}

void TranscricaoEditor::resized()
{
    if (constructed)       // no construtor o resize vem dos limites, não do usuário
    {
        proc.setPref ("largura", getWidth());
        proc.setPref ("altura", getHeight());
    }

    auto r = getLocalBounds().reduced (18, 14);
    headerArea = r.removeFromTop (kHeaderH);
    popoutBtn.setBounds (headerArea.getRight() - 96, headerArea.getY(), 96, 28);
    r.removeFromTop (6);

    statusArea = r.removeFromTop (22);
    r.removeFromTop (6);
    meterArea = r.removeFromTop (8);
    r.removeFromTop (4);
    messageArea = r.removeFromTop (30);
    r.removeFromTop (2);

    mainBtn.setBounds (r.removeFromTop (42));
    r.removeFromTop (10);
    advBtn.setBounds (r.removeFromTop (34));
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (68);
    if (advOpen)
    {
        // o painel ocupa o que der, sempre deixando espaço para a transcrição
        const int h = juce::jlimit (60, kAdvH, r.getHeight() - kMinTranscriptH - 10);
        advView.setBounds (r.removeFromTop (h));
        const int w = advView.getWidth() - (h < kAdvH ? advView.getScrollBarThickness() + 2 : 0);
        advPanel.setSize (w, kAdvH);
        layoutAdvanced (advPanel.getLocalBounds().reduced (12, 10));
        r.removeFromTop (10);
    }

    view.setBounds (r);

    bottom.removeFromTop (10);
    auto row1 = bottom.removeFromTop (32);
    const int w = (row1.getWidth() - 16) / 3;
    copyBtn.setBounds (row1.removeFromLeft (w));
    row1.removeFromLeft (8);
    clearBtn.setBounds (row1.removeFromLeft (w));
    row1.removeFromLeft (8);
    logsBtn.setBounds (row1);
    bottom.removeFromTop (4);
    fontLbl.setBounds (bottom.removeFromLeft (112));
    fontSize.setBounds (bottom.removeFromLeft (150));
    bottom.removeFromLeft (16);
    autoscroll.setBounds (bottom.removeFromLeft (juce::jmin (160, bottom.getWidth() / 2)));
    allChannels.setBounds (bottom);
}

void TranscricaoEditor::layoutAdvanced (juce::Rectangle<int> r)
{
    auto row = [&r] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (6); return x; };
    const int labelW = 120;

    auto a = row (26);
    kwLabel.setBounds (a.removeFromLeft (labelW));
    keywords.setBounds (a);

    a = row (26);
    modeLabel.setBounds (a.removeFromLeft (labelW));
    mode.setBounds (a.removeFromLeft (a.getWidth() / 2 - 6));
    a.removeFromLeft (12);
    langLabel.setBounds (a.removeFromLeft (52));
    language.setBounds (a);

    a = row (84);
    const int kw = juce::jmin (100, (a.getWidth() - 150) / 3);
    for (auto pair : { std::pair<juce::Slider*, juce::Label*> { &gate, &gateLbl },
                        std::pair<juce::Slider*, juce::Label*> { &sens, &sensLbl },
                        std::pair<juce::Slider*, juce::Label*> { &hold, &holdLbl } })
    {
        auto col = a.removeFromLeft (kw);
        pair.second->setBounds (col.removeFromTop (16));
        pair.first->setBounds (col);
        a.removeFromLeft (6);
    }
    partials.setBounds (a.removeFromTop (30));
    writeLog.setBounds (a.removeFromTop (30));

    a = row (26);
    vocabLabel.setBounds (a.removeFromLeft (labelW));
    vocabulary.setBounds (a);

    a = row (26);
    corrLabel.setBounds (a.removeFromLeft (labelW));
    corrections.setBounds (a);

    a = row (26);
    nameLabel.setBounds (a.removeFromLeft (labelW));
    channelName.setBounds (a);

    a = row (20);
    modelsLabel.setBounds (a.removeFromLeft (labelW));
    modelsInfo.setBounds (a);

    a = row (38);
    const int nb = downloadBtns.size();
    const int bw = (a.getWidth() - 100 - 6 * nb) / juce::jmax (1, nb);
    for (auto* b : downloadBtns) { b->setBounds (a.removeFromLeft (bw)); a.removeFromLeft (6); }
    openModels.setBounds (a);

    a = row (20);
    cancelDl.setBounds (a.removeFromRight (90));
    dlBar.setBounds (a.withTrimmedRight (6));
}
