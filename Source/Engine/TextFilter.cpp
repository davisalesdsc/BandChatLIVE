#include "TextFilter.h"

namespace tav::text
{

static juce::juce_wchar foldChar (juce::juce_wchar c)
{
    switch (c)
    {
        case 0xE1: case 0xE0: case 0xE2: case 0xE3: case 0xE4:
        case 0xC1: case 0xC0: case 0xC2: case 0xC3: case 0xC4: return 'a';
        case 0xE9: case 0xE8: case 0xEA: case 0xEB:
        case 0xC9: case 0xC8: case 0xCA: case 0xCB:            return 'e';
        case 0xED: case 0xEC: case 0xEE: case 0xEF:
        case 0xCD: case 0xCC: case 0xCE: case 0xCF:            return 'i';
        case 0xF3: case 0xF2: case 0xF4: case 0xF5: case 0xF6:
        case 0xD3: case 0xD2: case 0xD4: case 0xD5: case 0xD6: return 'o';
        case 0xFA: case 0xF9: case 0xFB: case 0xFC:
        case 0xDA: case 0xD9: case 0xDB: case 0xDC:            return 'u';
        case 0xE7: case 0xC7:                                  return 'c';
        case 0xF1: case 0xD1:                                  return 'n';
        default: break;
    }
    return juce::CharacterFunctions::toLowerCase (c);
}

juce::String fold (const juce::String& s)
{
    juce::String out;
    out.preallocateBytes ((size_t) s.getNumBytesAsUTF8() + 4);
    for (auto p = s.getCharPointer(); ! p.isEmpty(); )
        out += juce::String::charToString (foldChar (p.getAndAdvance()));
    return out;
}

static bool isWordChar (juce::juce_wchar c)
{
    return juce::CharacterFunctions::isLetterOrDigit (c) || c > 0x7F;
}

//==============================================================================
static const char* const kHallucinations[] = {
    "legendas pela comunidade", "legenda pela comunidade", "legendas pelo comunidade",
    "amara.org", "transcricao e legendas", "legendado por", "legendas por",
    "obrigado por assistir", "obrigada por assistir", "inscreva-se", "inscreva se",
    "deixe o seu like", "deixe seu like", "nao se esqueca de se inscrever",
    "ative o sininho", "ativar o sininho", "ate o proximo video", "www.", "[musica]",
    "\xe2\x99\xaa" /* ♪ */, "subtitles by", "thanks for watching",
};

static juce::StringArray words (const juce::String& folded)
{
    juce::StringArray w;
    juce::String cur;
    for (auto p = folded.getCharPointer(); ! p.isEmpty(); )
    {
        auto c = p.getAndAdvance();
        if (isWordChar (c)) cur += juce::String::charToString (c);
        else if (cur.isNotEmpty()) { w.add (cur); cur.clear(); }
    }
    if (cur.isNotEmpty()) w.add (cur);
    return w;
}

bool isHallucination (const juce::String& s)
{
    const auto f = fold (s);
    for (auto* h : kHallucinations)
        if (f.contains (juce::String::fromUTF8 (h)))
            return true;

    // repetição em loop: a mesma sequência de 1 a 3 palavras 4+ vezes seguidas
    const auto w = words (f);
    for (int n = 1; n <= 3; ++n)
    {
        for (int i = 0; i + n * 4 <= w.size(); ++i)
        {
            int reps = 1;
            for (int j = i + n; j + n <= w.size(); j += n)
            {
                bool same = true;
                for (int k = 0; k < n && same; ++k)
                    same = w[i + k] == w[j + k];
                if (! same) break;
                ++reps;
            }
            if (reps >= 4) return true;
        }
    }
    return false;
}

juce::String clean (const juce::String& s)
{
    auto t = s.replaceCharacters ("\t\r\n", "   ");
    while (t.contains ("  ")) t = t.replace ("  ", " ");
    t = t.trim();

    int letters = 0;
    for (auto p = t.getCharPointer(); ! p.isEmpty(); )
        if (isWordChar (p.getAndAdvance())) ++letters;
    if (letters < 2 || isHallucination (t))
        return {};
    return t;
}

//==============================================================================
std::vector<Correction> parseCorrections (const juce::String& spec)
{
    std::vector<Correction> out;
    for (auto& part : juce::StringArray::fromTokens (spec, ";", ""))
    {
        if (! part.contains ("=>")) continue;
        auto wrong = part.upToFirstOccurrenceOf ("=>", false, false).trim();
        auto right = part.fromFirstOccurrenceOf ("=>", false, false).trim();
        if (wrong.isNotEmpty())
            out.push_back ({ fold (wrong), right });
    }
    return out;
}

juce::String applyCorrections (const juce::String& s, const std::vector<Correction>& list)
{
    auto result = s;
    for (auto& c : list)
    {
        const int len = c.wrongFolded.length();
        int from = 0;
        for (;;)
        {
            const auto f = fold (result);          // mesmo tamanho que result
            const int i = f.indexOf (from, c.wrongFolded);
            if (i < 0) break;
            const bool startOk = i == 0 || ! isWordChar (f[i - 1]);
            const bool endOk   = i + len >= f.length() || ! isWordChar (f[i + len]);
            if (startOk && endOk)
            {
                result = result.substring (0, i) + c.right + result.substring (i + len);
                from = i + c.right.length();
            }
            else
            {
                from = i + 1;
            }
        }
    }
    return result;
}

juce::StringArray parseKeywords (const juce::String& spec)
{
    juce::StringArray out;
    for (auto& k : juce::StringArray::fromTokens (spec, ",", ""))
    {
        auto t = fold (k.trim());
        if (t.isNotEmpty()) out.add (t);
    }
    return out;
}

bool containsKeyword (const juce::String& s, const juce::StringArray& foldedKeywords)
{
    if (foldedKeywords.isEmpty()) return false;
    const auto f = fold (s);
    for (auto& k : foldedKeywords)
        if (f.contains (k)) return true;
    return false;
}

} // namespace tav::text
