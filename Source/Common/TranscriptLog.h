#pragma once

#include <juce_core/juce_core.h>

namespace tav
{

/** Histórico diário das frases finais: Logs/transcricao_AAAA-MM-DD.txt
    (mesmo formato do script do REAPER: "[HH:MM:SS] Canal: texto"). */
void appendTranscript (juce::int64 timeMs, const juce::String& channel, const juce::String& text);

/** Log técnico (transcritor.log na pasta de dados), para suporte. */
void debugLog (const juce::String& area, const juce::String& message);

} // namespace tav
