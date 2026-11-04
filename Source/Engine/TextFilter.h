#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace tav
{

/** Ferramentas de texto: limpeza do que o Whisper devolve, correções do
    usuário e busca de palavras-chave sem diferenciar acento e maiúscula. */
namespace text
{
    /** minúsculo e sem acento, MESMO número de caracteres que a entrada
        (para que os índices continuem valendo no texto original). */
    juce::String fold (const juce::String& s);

    /** true para as frases que o Whisper inventa em silêncio/ruído
        ("legendas pela comunidade", "obrigado por assistir"...) e para
        repetições em loop ("não não não não"). */
    bool isHallucination (const juce::String& s);

    /** Espaços normalizados; vazio se for alucinação ou curto demais. */
    juce::String clean (const juce::String& s);

    struct Correction { juce::String wrongFolded, right; };

    /** "david => Davi ; in ear => in-ear" -> lista de correções */
    std::vector<Correction> parseCorrections (const juce::String& spec);

    /** Troca palavras inteiras (sem diferenciar acento/maiúscula). */
    juce::String applyCorrections (const juce::String& s, const std::vector<Correction>& list);

    /** "fone, volume, retorno" -> {"fone","volume","retorno"} já dobrados */
    juce::StringArray parseKeywords (const juce::String& spec);

    bool containsKeyword (const juce::String& s, const juce::StringArray& foldedKeywords);
}

} // namespace tav
