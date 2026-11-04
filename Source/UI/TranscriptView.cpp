#include "TranscriptView.h"
#include "Theme.h"
#include "../Engine/TextFilter.h"

using Kind = tav::Hub::Kind;

TranscriptView::TranscriptView (TranscricaoProcessor& p) : proc (p)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    startTimerHz (15);
}

TranscriptView::~TranscriptView() { stopTimer(); }

void TranscriptView::resized()
{
    viewport.setBounds (getLocalBounds().reduced (10, 8));
    rebuild();
}

void TranscriptView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    if (lines.empty())
    {
        const bool on = proc.getParams().getRawParameterValue ("transcrever")->load() > 0.5f;
        g.setColour (theme::textMuted);
        g.setFont (theme::font (theme::transcriptFontSize ((int) proc.getPref ("fonte", 3)) * 0.7f));
        g.drawFittedText (on ? TranscricaoProcessor::u8 ("Aguardando fala...")
                             : TranscricaoProcessor::u8 ("Transcrição pausada. Clique em INICIAR."),
                          getLocalBounds().reduced (16), juce::Justification::centredTop, 2);
    }
}

void TranscriptView::timerCallback()
{
    auto& hub = proc.getTranscriber().getHub();
    const int v = hub.getVersion();
    const int font = (int) proc.getPref ("fonte", 3);
    const auto kw = proc.getPref ("palavras", "").toString();
    const bool all = (bool) proc.getPref ("todos", true);
    if (v != seenVersion || font != builtFont || kw != builtKeywords || all != builtAll)
    {
        builtAll = all;
        auto& vp = viewport;
        followBottom = (bool) proc.getPref ("autoscroll", true)
                    && vp.getViewPositionY() + vp.getViewHeight() >= content.getHeight() - 40;
        if (v != seenVersion && (bool) proc.getPref ("autoscroll", true) && seenVersion < 0)
            followBottom = true;
        lines = hub.snapshot (seenVersion);
        if (! all)
        {
            const auto me = proc.getTranscriber().getSourceId();
            lines.erase (std::remove_if (lines.begin(), lines.end(),
                                         [&] (const tav::Hub::Entry& e) { return e.source != me; }), lines.end());
        }
        rebuild();
        repaint();
    }
}

void TranscriptView::rebuild()
{
    const int width = viewport.getMaximumVisibleWidth();
    const int fontIdx = (int) proc.getPref ("fonte", 3);
    const auto kwSpec = proc.getPref ("palavras", "").toString();
    builtWidth = width; builtFont = fontIdx; builtKeywords = kwSpec;
    if (width <= 40) return;

    const auto keywords = tav::text::parseKeywords (kwSpec);
    const float size = theme::transcriptFontSize (fontIdx);
    const auto timeFont = theme::font (size * 0.62f);
    const float timeW = juce::GlyphArrangement::getStringWidth (timeFont, "00:00:00") + 14.0f;
    const float textW = (float) width - timeW - 4.0f;

    rows.clear();
    float y = 4.0f;
    for (auto& l : lines)
    {
        Row row;
        row.time = juce::Time (l.timeMs).formatted ("%H:%M:%S");
        const bool hot = l.kind == Kind::final && tav::text::containsKeyword (l.text, keywords);

        juce::AttributedString as;
        as.setWordWrap (juce::AttributedString::byWord);
        // nome do canal em negrito, na cor do canal: "Comunicação Davi: texto"
        if (l.channel.isNotEmpty())
            as.append (l.channel + ": ", theme::font (l.kind == Kind::system ? size * 0.6f : size, true),
                       juce::Colour (l.colour).withMultipliedAlpha (l.kind == Kind::partial ? 0.6f : 1.0f));
        if (l.kind == Kind::system)
        {
            as.append (l.text, theme::font (size * 0.6f), theme::warn);
            row.timeColour = theme::textMuted;
        }
        else if (l.kind == Kind::partial)
        {
            // ainda falando: apagado, com reticências; vira a frase final no lugar
            as.append (l.text + TranscricaoProcessor::u8 (" …"), theme::font (size).italicised(), theme::textDim);
            row.timeColour = theme::textMuted;
        }
        else
        {
            as.append (l.text, theme::font (size), hot ? theme::accentHv : theme::text);
            row.timeColour = hot ? theme::accentHv : theme::textMuted;
        }
        row.layout.createLayout (as, juce::jmax (40.0f, textW));
        row.y = y;
        row.h = juce::jmax (row.layout.getHeight(), size * 0.8f);
        y += row.h + size * 0.35f;
        rows.push_back (std::move (row));
    }

    content.setSize (width, (int) std::ceil (y + 4.0f));
    if (followBottom)
        viewport.setViewPosition (0, juce::jmax (0, content.getHeight() - viewport.getViewHeight()));
}

void TranscriptView::Content::paint (juce::Graphics& g)
{
    const float size = theme::transcriptFontSize (owner.builtFont);
    const auto timeFont = theme::font (size * 0.62f);
    const float timeW = juce::GlyphArrangement::getStringWidth (timeFont, "00:00:00") + 14.0f;
    const auto clip = g.getClipBounds().toFloat();

    for (auto& r : owner.rows)
    {
        if (r.y + r.h < clip.getY() || r.y > clip.getBottom()) continue;
        g.setFont (timeFont);
        g.setColour (r.timeColour);
        g.drawText (r.time, juce::Rectangle<float> (0.0f, r.y + size * 0.12f, timeW, size * 0.8f),
                    juce::Justification::topLeft, false);
        r.layout.draw (g, { timeW, r.y, (float) getWidth() - timeW, r.h });
    }
}

juce::String TranscriptView::finalTextForCopy() const
{
    juce::StringArray out;
    for (auto& l : lines)
        if (l.kind == Kind::final)
            out.add ("[" + juce::Time (l.timeMs).formatted ("%H:%M:%S") + "] "
                     + (l.channel.isNotEmpty() ? l.channel + ": " : juce::String()) + l.text);
    return out.joinIntoString ("\n");
}
