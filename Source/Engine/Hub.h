#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace tav
{

/** A conversa compartilhada entre todos os BandChatLIVE abertos no mesmo
    programa (DAW/host). Cada instância (um canal de comunicação) publica as
    suas falas aqui com o nome do canal, e a janela de QUALQUER instância
    mostra todas: qualquer uma serve de "matriz".

    Um por processo, criado pela primeira instância e destruído com a última. */
class Hub
{
public:
    enum class Kind { final, partial, system };

    struct Entry
    {
        juce::String id;          // único no processo (fonte + fala)
        juce::int64  timeMs = 0;
        juce::String source;      // qual instância publicou
        juce::String channel;     // nome mostrado antes do texto ("" = sem nome)
        juce::uint32 colour = 0;  // ARGB da cor do canal
        juce::String text;
        Kind         kind = Kind::final;
    };

    static std::shared_ptr<Hub> get();

    /** Entra/sai da conversa. Devolve o número do canal (1, 2, 3...), usado
        como nome quando o host não informa o nome da faixa. */
    int  addSource (const juce::String& source);
    void removeSource (const juce::String& source);
    int  getNumSources() const;

    /** Insere ou atualiza (mesmo id: a parcial vira a frase final no lugar).
        Texto vazio num final apaga a linha. */
    void publish (const Entry& e);

    std::vector<Entry> snapshot (int& version) const;
    int  getVersion() const noexcept { return version.load(); }
    void clear();

    /** Paleta para canais sem cor no host (bem distintas num fundo escuro). */
    static juce::uint32 paletteColour (int channelNumber);

private:
    Hub() = default;
    mutable std::mutex lock;
    std::vector<Entry> entries;
    std::vector<std::pair<juce::String, int>> sources;
    std::atomic<int> version { 0 };
};

} // namespace tav
