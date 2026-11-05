#include "PopoutWindow.h"

static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

PopoutWindow::PopoutWindow (TranscricaoProcessor& p)
    : juce::DocumentWindow (u8 ("BandChatLIVE"), theme::bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      proc (p)
{
    setLookAndFeel (&lnf);
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setResizeLimits (360, 220, 4000, 3000);
    setContentOwned (new Body (p, *this), false);

    const auto saved = p.getPref ("popGeom", "").toString();
    if (saved.isNotEmpty()) restoreWindowStateFromString (saved);
    else centreWithSize (800, 420);

    setAlwaysOnTop ((bool) p.getPref ("popFixo", true));
    setVisible (true);
}

PopoutWindow::~PopoutWindow()
{
    proc.setPref ("popGeom", getWindowStateAsString());
    clearContentComponent();
    setLookAndFeel (nullptr);
}

void PopoutWindow::closeButtonPressed()
{
    // não dá para se destruir dentro do próprio callback: agenda
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PopoutWindow> (this)]
    {
        if (safe != nullptr) safe->proc.closePopout();
    });
}

//==============================================================================
PopoutWindow::Body::Body (TranscricaoProcessor& p, PopoutWindow& w) : proc (p), window (w), view (p)
{
    pin.setToggleState ((bool) p.getPref ("popFixo", true), juce::dontSendNotification);
    pin.onClick = [this] { proc.setPref ("popFixo", pin.getToggleState()); window.setAlwaysOnTop (pin.getToggleState()); };
    autoscroll.setToggleState ((bool) p.getPref ("autoscroll", true), juce::dontSendNotification);
    autoscroll.onClick = [this] { proc.setPref ("autoscroll", autoscroll.getToggleState()); };
    allChannels.setToggleState ((bool) p.getPref ("todos", true), juce::dontSendNotification);
    allChannels.onClick = [this] { proc.setPref ("todos", allChannels.getToggleState()); };

    fontSize.setSliderStyle (juce::Slider::LinearHorizontal);
    fontSize.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fontSize.setRange (1, 5, 1);
    fontSize.setValue ((int) p.getPref ("fonte", 3), juce::dontSendNotification);
    fontSize.setTooltip ("Tamanho do texto");
    fontSize.onValueChange = [this] { proc.setPref ("fonte", (int) fontSize.getValue()); };

    startStop.onClick = [this]
    {
        auto* prm = proc.getParams().getParameter ("transcrever");
        prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
    };
    copy.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (view.finalTextForCopy()); };

    for (juce::Component* c : { (juce::Component*) &pin, (juce::Component*) &autoscroll, (juce::Component*) &allChannels, (juce::Component*) &fontSize,
                                (juce::Component*) &startStop, (juce::Component*) &copy, (juce::Component*) &view })
        addAndMakeVisible (c);
    setSize (800, 420);
    timerCallback();
    startTimerHz (4);
}

void PopoutWindow::Body::timerCallback()
{
    const bool on = proc.getParams().getRawParameterValue ("transcrever")->load() > 0.5f;
    startStop.setButtonText (on ? "PAUSAR" : "INICIAR");
    startStop.getProperties().set ("primary", ! on);
    const auto st = proc.getTranscriber().getState();
    if (! on)                                       { status = u8 ("pausado");         statusColour = theme::textMuted; }
    else if (! proc.isReceivingAudio())             { status = u8 ("sem áudio"); statusColour = theme::warn; }
    else if (proc.getAudioStatus() == TranscricaoProcessor::AudioStatus::silence) { status = u8 ("sem sinal"); statusColour = theme::warn; }
    else if (st == tav::Transcriber::State::noModel) { status = u8 ("sem modelo");      statusColour = theme::warn; }
    else if (st == tav::Transcriber::State::loading) { status = u8 ("carregando...");   statusColour = theme::warn; }
    else if (st == tav::Transcriber::State::error)   { status = u8 ("erro");            statusColour = theme::error; }
    else                                             { status = u8 ("ouvindo");         statusColour = theme::ok; }
    repaint();
}

void PopoutWindow::Body::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto bar = r.removeFromTop (28);
    startStop.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (8);
    copy.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (10);
    pin.setBounds (bar.removeFromLeft (140));
    autoscroll.setBounds (bar.removeFromLeft (90));
    allChannels.setBounds (bar.removeFromLeft (140));
    fontSize.setBounds (bar.removeFromLeft (110));
    r.removeFromTop (8);
    view.setBounds (r);
}

void PopoutWindow::Body::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    auto r = getLocalBounds().reduced (10).removeFromTop (28);
    r.removeFromLeft (90 + 8 + 80 + 10 + 140 + 90 + 140 + 110 + 10);
    if (r.getWidth() < 60) return;
    g.setColour (statusColour);
    g.fillEllipse ((float) r.getX(), (float) r.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (14.0f));
    g.drawText (status, r.withTrimmedLeft (16), juce::Justification::centredLeft);
}
