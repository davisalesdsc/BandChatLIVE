#include "GlobalPrefs.h"
#include "../Engine/Models.h"
#include <mutex>

namespace tav
{

std::shared_ptr<GlobalPrefs> GlobalPrefs::get()
{
    static std::mutex m;
    static std::weak_ptr<GlobalPrefs> current;
    std::lock_guard<std::mutex> g (m);
    if (auto p = current.lock()) return p;
    std::shared_ptr<GlobalPrefs> p (new GlobalPrefs());
    current = p;
    return p;
}

GlobalPrefs::~GlobalPrefs()
{
    stopTimer();
    if (file != nullptr) flush();
}

GlobalPrefs::GlobalPrefs()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "preferencias";
    o.filenameSuffix      = ".xml";
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 500;        // grava sozinho logo depois de digitar
    o.processLock         = &processLock;    // app e DAW abertos ao mesmo tempo
    path = models::dataDir().getChildFile ("preferencias.xml");
    file = std::make_unique<juce::PropertiesFile> (path, o);
    lastSeen = path.loadFileAsString();
    startTimer (1000);
}

bool GlobalPrefs::reloadIfChangedOutside() const
{
    // compara o CONTEÚDO (a data de modificação tem resolução de 1 s e
    // perdia duas gravações no mesmo segundo)
    const juce::ScopedLock sl (reloadLock);
    const auto now = path.loadFileAsString();
    if (now == lastSeen) return false;
    if (file->needsToBeSaved()) file->saveIfNeeded();   // mudança nossa pendente: vale a nossa
    else                        file->reload();
    lastSeen = path.loadFileAsString();
    return true;
}

void GlobalPrefs::timerCallback()
{
    if (reloadIfChangedOutside())
        sendChangeMessage();
}

bool GlobalPrefs::isGlobal (const juce::Identifier& key)
{
    static const juce::Identifier keys[] = { "correcoes", "vocabulario", "palavras", "modo", "idioma",
                                             "fonte", "autoscroll", "todos", "log", "popFixo", "avancado" };
    for (auto& k : keys)
        if (k == key) return true;
    return false;
}

bool GlobalPrefs::has (const juce::Identifier& key) const
{
    return file->containsKey (key.toString());
}

juce::var GlobalPrefs::getValue (const juce::Identifier& key, const juce::var& def) const
{
    if (! file->containsKey (key.toString())) return def;
    const auto s = file->getValue (key.toString());
    // PropertiesFile guarda texto: devolve no mesmo tipo do padrão
    if (def.isBool())   return s.getIntValue() != 0 || s == "true";
    if (def.isInt() || def.isInt64()) return s.getIntValue();
    if (def.isDouble()) return s.getDoubleValue();
    return s;
}

void GlobalPrefs::setValue (const juce::Identifier& key, const juce::var& v)
{
    reloadIfChangedOutside();
    const auto text = v.isBool() ? juce::String ((bool) v ? "1" : "0") : v.toString();
    if (file->containsKey (key.toString()) && file->getValue (key.toString()) == text)
        return;
    file->setValue (key.toString(), text);
    sendChangeMessage();                     // os outros canais abertos atualizam
}

void GlobalPrefs::flush()
{
    const juce::ScopedLock sl (reloadLock);
    file->saveIfNeeded();
    lastSeen = path.loadFileAsString();
}

} // namespace tav
