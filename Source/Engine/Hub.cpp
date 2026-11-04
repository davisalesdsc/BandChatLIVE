#include "Hub.h"

namespace tav
{

static constexpr size_t kMaxEntries = 3000;

std::shared_ptr<Hub> Hub::get()
{
    static std::mutex m;
    static std::weak_ptr<Hub> current;
    std::lock_guard<std::mutex> g (m);
    if (auto h = current.lock()) return h;
    std::shared_ptr<Hub> h (new Hub());
    current = h;
    return h;
}

int Hub::addSource (const juce::String& source)
{
    std::lock_guard<std::mutex> g (lock);
    // menor número livre: fechar o canal 2 e abrir outro reaproveita o "2"
    int n = 1;
    for (bool taken = true; taken; )
    {
        taken = false;
        for (auto& s : sources) if (s.second == n) { taken = true; ++n; break; }
    }
    sources.emplace_back (source, n);
    version.fetch_add (1);
    return n;
}

void Hub::removeSource (const juce::String& source)
{
    std::lock_guard<std::mutex> g (lock);
    sources.erase (std::remove_if (sources.begin(), sources.end(),
                                   [&] (auto& s) { return s.first == source; }), sources.end());
    version.fetch_add (1);
}

int Hub::getNumSources() const
{
    std::lock_guard<std::mutex> g (lock);
    return (int) sources.size();
}

void Hub::publish (const Entry& e)
{
    {
        std::lock_guard<std::mutex> g (lock);
        int found = -1;
        if (e.kind != Kind::system)
            for (int i = (int) entries.size() - 1; i >= juce::jmax (0, (int) entries.size() - 80); --i)
                if (entries[(size_t) i].id == e.id) { found = i; break; }

        if (found >= 0)
        {
            if (e.text.isEmpty()) entries.erase (entries.begin() + found);
            else
            {
                auto& x = entries[(size_t) found];
                x.text = e.text; x.kind = e.kind; x.channel = e.channel; x.colour = e.colour;
            }
        }
        else if (e.text.isNotEmpty())
        {
            // em ordem de horário: com vários canais, uma frase longa de um
            // pode terminar depois de uma curta de outro que começou depois
            auto pos = entries.end();
            while (pos != entries.begin() && std::prev (pos)->timeMs > e.timeMs) --pos;
            entries.insert (pos, e);
            if (entries.size() > kMaxEntries)
                entries.erase (entries.begin(), entries.begin() + (ptrdiff_t) (entries.size() - kMaxEntries));
        }
    }
    version.fetch_add (1);
}

std::vector<Hub::Entry> Hub::snapshot (int& v) const
{
    std::lock_guard<std::mutex> g (lock);
    v = version.load();
    return entries;
}

void Hub::clear()
{
    { std::lock_guard<std::mutex> g (lock); entries.clear(); }
    version.fetch_add (1);
}

juce::uint32 Hub::paletteColour (int n)
{
    static const juce::uint32 p[] = { 0xff59d18a, 0xff4fc3f7, 0xffffb74d, 0xfff06292, 0xffba68c8,
                                      0xffaed581, 0xffff8a65, 0xff4dd0e1, 0xfffff176, 0xff90a4ae };
    return p[(size_t) juce::jmax (0, n - 1) % std::size (p)];
}

} // namespace tav
