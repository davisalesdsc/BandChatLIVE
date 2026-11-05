#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <memory>

namespace tav
{

/** Preferências que valem para o computador inteiro, não para um projeto:
    correções, vocabulário, palavras destacadas, modo, idioma, tamanho do
    texto... Ficam em <pasta do BandChatLIVE>/preferencias.xml.

    Antes elas moravam só no estado do plugin dentro da sessão do DAW: abrir
    outro projeto, outro canal ou o app de novo voltava tudo ao padrão e o que
    a pessoa tinha digitado "sumia". Agora todas as instâncias (e o app) leem e
    gravam aqui, e uma mudança num canal vale na hora para os outros. */
class GlobalPrefs : public juce::ChangeBroadcaster,
                    private juce::Timer
{
public:
    /** Uma por processo: criada pelo primeiro canal, gravada e destruída com o último. */
    static std::shared_ptr<GlobalPrefs> get();
    ~GlobalPrefs() override;

    /** As chaves que são globais (as outras continuam por canal/sessão). */
    static bool isGlobal (const juce::Identifier& key);

    bool       has (const juce::Identifier& key) const;
    juce::var  getValue (const juce::Identifier& key, const juce::var& def) const;
    void       setValue (const juce::Identifier& key, const juce::var& v);

    /** Grava já (o normal é gravar sozinho meio segundo depois da última mudança). */
    void flush();

private:
    GlobalPrefs();
    /** O app e o DAW podem estar abertos juntos: se o arquivo mudou por fora
        (e aqui não há mudança pendente), relê antes de usar. */
    bool reloadIfChangedOutside() const;      // true se releu
    void timerCallback() override;           // vigia o arquivo: mudou por fora -> avisa os canais
    juce::File path;
    mutable juce::String lastSeen;           // conteúdo do arquivo da última leitura/gravação nossa
    mutable juce::CriticalSection reloadLock;
    juce::InterProcessLock processLock { "BandChatLIVE.preferencias" };   // antes do arquivo: vive mais que ele
    std::unique_ptr<juce::PropertiesFile> file;
};

} // namespace tav
