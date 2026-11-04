#include "TranscriptLog.h"
#include "../Engine/Models.h"

namespace tav
{

static juce::CriticalSection& fileLock()
{
    static juce::CriticalSection l;
    return l;
}

void appendTranscript (juce::int64 timeMs, const juce::String& channel, const juce::String& text)
{
    if (text.isEmpty()) return;
    const juce::Time t (timeMs);
    const auto file = models::logsDir().getChildFile (t.formatted ("transcricao_%Y-%m-%d.txt"));
    auto line = "[" + t.formatted ("%H:%M:%S") + "] "
              + (channel.isNotEmpty() ? channel + ": " : juce::String()) + text + "\n";
    const juce::ScopedLock sl (fileLock());
    file.appendText (line, false, false, "\n");
}

void debugLog (const juce::String& area, const juce::String& message)
{
    const juce::ScopedLock sl (fileLock());
    auto file = models::dataDir().getChildFile ("transcritor.log");
    if (file.getSize() > 2 * 1024 * 1024)
    {
        auto old = file.getSiblingFile ("transcritor.old.log");
        old.deleteFile();
        file.moveFileTo (old);
    }
    file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S ")
                     + "[" + area + "] " + message + "\n", false, false, "\n");
}

} // namespace tav
