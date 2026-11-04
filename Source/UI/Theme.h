#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Paleta "Máquina de VS" (a mesma do script do REAPER): preto quase puro
    + vermelho vibrante. */
namespace theme
{
    const juce::Colour bg        { 0xff0a0a0c };
    const juce::Colour panel     { 0xff131013 };
    const juce::Colour panelAlt  { 0xff1a1518 };
    const juce::Colour border    { 0xff2c1416 };
    const juce::Colour accent    { 0xffc81620 };
    const juce::Colour accentHv  { 0xffff3b3b };
    const juce::Colour accentDk  { 0xff6e0f13 };
    const juce::Colour text      { 0xfff2f2f4 };
    const juce::Colour textDim   { 0xff9a959a };
    const juce::Colour textMuted { 0xff615c60 };
    const juce::Colour ok        { 0xff59d18a };
    const juce::Colour warn      { 0xffe0b354 };
    const juce::Colour error     { 0xffff4b55 };

    /** Tamanhos do texto da transcrição (roleta 1..5). */
    inline float transcriptFontSize (int idx)
    {
        static const float s[] = { 18.0f, 24.0f, 30.0f, 38.0f, 46.0f };
        return s[juce::jlimit (0, 4, idx - 1)];
    }

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColour (juce::ResizableWindow::backgroundColourId, bg);
            setColour (juce::DocumentWindow::backgroundColourId, bg);
            setColour (juce::Label::textColourId, text);
            setColour (juce::TextEditor::backgroundColourId, panelAlt);
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::outlineColourId, border);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.5f));
            setColour (juce::ComboBox::backgroundColourId, panelAlt);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::outlineColourId, border);
            setColour (juce::ComboBox::arrowColourId, accentHv);
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
            setColour (juce::Slider::thumbColourId, accentHv);
            setColour (juce::Slider::trackColourId, accent);
            setColour (juce::Slider::backgroundColourId, panelAlt);
            setColour (juce::Slider::rotarySliderFillColourId, accent);
            setColour (juce::Slider::rotarySliderOutlineColourId, panelAlt);
            setColour (juce::Slider::textBoxTextColourId, textDim);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::ToggleButton::textColourId, text);
            setColour (juce::ToggleButton::tickColourId, accentHv);
            setColour (juce::ToggleButton::tickDisabledColourId, textMuted);
            setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3a1418));
            setColour (juce::TooltipWindow::backgroundColourId, panel);
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, border);
            setColour (juce::ProgressBar::backgroundColourId, panelAlt);
            setColour (juce::ProgressBar::foregroundColourId, accent);
        }

        /** Botões: "primary" (gradiente vermelho) ou secundário (escuro com
            contorno), conforme a propriedade "primary" do componente. */
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                   bool hover, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool primary = (bool) b.getProperties().getWithDefault ("primary", false);
            if (! b.isEnabled())
            {
                g.setColour (juce::Colour (0xff1a1215)); g.fillRoundedRectangle (r, 6.0f);
                g.setColour (juce::Colour (0xff2e2225)); g.drawRoundedRectangle (r, 6.0f, 1.0f);
                return;
            }
            if (primary)
            {
                auto top = down ? juce::Colour (0xff8a0d13) : (hover ? accentHv : accent);
                auto bot = down ? juce::Colour (0xff4f0a0d) : (hover ? juce::Colour (0xff9e1418) : accentDk);
                g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
            }
            else
            {
                g.setColour (down ? juce::Colours::black : (hover ? juce::Colour (0xff1c1216) : juce::Colour (0xff121013)));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (hover || b.getToggleState() ? accentHv : juce::Colour (0xff35262a));
                g.drawRoundedRectangle (r, 6.0f, 1.3f);
            }
        }

        juce::Font getTextButtonFont (juce::TextButton& b, int h) override
        {
            const bool twoLines = b.getButtonText().containsChar ('\n');
            return font (juce::jmin (twoLines ? 12.5f : 15.0f, (float) h * (twoLines ? 0.34f : 0.45f)), true);
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setFont (getTextButtonFont (b, b.getHeight()));
            g.setColour (b.isEnabled() ? text : textMuted);
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred,
                              b.getButtonText().containsChar ('\n') ? 2 : 1);
        }
    };
}
