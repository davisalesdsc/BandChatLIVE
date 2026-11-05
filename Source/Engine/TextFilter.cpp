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

//==============================================================================
namespace
{
    // bytes 0x80..0xFF -> caractere, em cada codificação antiga
    const juce::uint16 kMacRoman[128] = {
        0x00c4, 0x00c5, 0x00c7, 0x00c9, 0x00d1, 0x00d6, 0x00dc, 0x00e1,
        0x00e0, 0x00e2, 0x00e4, 0x00e3, 0x00e5, 0x00e7, 0x00e9, 0x00e8,
        0x00ea, 0x00eb, 0x00ed, 0x00ec, 0x00ee, 0x00ef, 0x00f1, 0x00f3,
        0x00f2, 0x00f4, 0x00f6, 0x00f5, 0x00fa, 0x00f9, 0x00fb, 0x00fc,
        0x2020, 0x00b0, 0x00a2, 0x00a3, 0x00a7, 0x2022, 0x00b6, 0x00df,
        0x00ae, 0x00a9, 0x2122, 0x00b4, 0x00a8, 0x2260, 0x00c6, 0x00d8,
        0x221e, 0x00b1, 0x2264, 0x2265, 0x00a5, 0x00b5, 0x2202, 0x2211,
        0x220f, 0x03c0, 0x222b, 0x00aa, 0x00ba, 0x03a9, 0x00e6, 0x00f8,
        0x00bf, 0x00a1, 0x00ac, 0x221a, 0x0192, 0x2248, 0x2206, 0x00ab,
        0x00bb, 0x2026, 0x00a0, 0x00c0, 0x00c3, 0x00d5, 0x0152, 0x0153,
        0x2013, 0x2014, 0x201c, 0x201d, 0x2018, 0x2019, 0x00f7, 0x25ca,
        0x00ff, 0x0178, 0x2044, 0x20ac, 0x2039, 0x203a, 0xfb01, 0xfb02,
        0x2021, 0x00b7, 0x201a, 0x201e, 0x2030, 0x00c2, 0x00ca, 0x00c1,
        0x00cb, 0x00c8, 0x00cd, 0x00ce, 0x00cf, 0x00cc, 0x00d3, 0x00d4,
        0xf8ff, 0x00d2, 0x00da, 0x00db, 0x00d9, 0x0131, 0x02c6, 0x02dc,
        0x00af, 0x02d8, 0x02d9, 0x02da, 0x00b8, 0x02dd, 0x02db, 0x02c7,
    };
    const juce::uint16 kCp1252[128] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
        0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,
        0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
        0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
        0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
        0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,
        0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
        0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
        0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
        0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
        0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
        0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7,
        0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff,
    };

    /** Volta cada caractere ao byte da codificação antiga; falso se algum
        caractere não existe nela (então o texto não veio dela). */
    bool toLegacyBytes (const juce::String& s, const juce::uint16 (&table)[128], juce::MemoryBlock& out)
    {
        out.reset();
        for (auto p = s.getCharPointer(); ! p.isEmpty(); )
        {
            const auto c = p.getAndAdvance();
            if (c < 0x80) { const char b = (char) c; out.append (&b, 1); continue; }
            int found = -1;
            for (int i = 0; i < 128 && found < 0; ++i)
                if (table[i] == c) found = i;
            if (found < 0) return false;
            const char b = (char) (0x80 + found);
            out.append (&b, 1);
        }
        return true;
    }
}

juce::String repairMisdecodedUtf8 (const juce::String& s)
{
    bool hasNonAscii = false;
    for (auto p = s.getCharPointer(); ! p.isEmpty(); )
        if (p.getAndAdvance() >= 0x80) { hasNonAscii = true; break; }
    if (! hasNonAscii) return s;

    juce::MemoryBlock bytes;
    for (auto* table : { &kMacRoman, &kCp1252 })
    {
        if (! toLegacyBytes (s, *table, bytes)) continue;
        const auto* d = static_cast<const char*> (bytes.getData());
        const auto n = (int) bytes.getSize();
        // só conserta se os bytes formam UTF-8 válido COM caracteres de mais
        // de um byte (texto legítimo como "Ação" não vira UTF-8 válido)
        if (juce::CharPointer_UTF8::isValidString (d, n))
        {
            auto fixed = juce::String::fromUTF8 (d, n);
            // só aceita se der letras de verdade (latim com acento), não
            // acentos soltos/símbolos de uma leitura na tabela errada
            bool plausible = fixed.length() < s.length();
            for (auto q = fixed.getCharPointer(); plausible && ! q.isEmpty(); )
            {
                const auto c = q.getAndAdvance();
                plausible = c < 0x80 || (c >= 0xA0 && c <= 0x17F);
            }
            if (plausible) return fixed;
        }
    }
    return s;
}

} // namespace tav::text
