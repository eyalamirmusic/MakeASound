#include "MidiPortRegistry.h"

namespace MakeASound
{

void MidiPortRegistry::beginScan()
{
    for (auto& entry: entries)
    {
        entry.portNumber = -1;
        entry.claimed = false;
    }
}

int MidiPortRegistry::idFor(const std::string& identity, int portNumber)
{
    for (auto i = 0; i < entries.size(); ++i)
    {
        auto& entry = entries.get(i);

        if (entry.claimed || entry.identity != identity)
            continue;

        entry.portNumber = portNumber;
        entry.claimed = true;

        return i;
    }

    // Either the first time this identity has been seen, or a second port wearing
    // it: both get a slot of their own, and keep it for as long as the registry
    // lives.
    entries.add(Entry {identity, portNumber, true});

    return entries.getLastElementIndex();
}

int MidiPortRegistry::portNumberForId(int id) const
{
    if (id < 0 || id >= entries.size())
        return -1;

    return entries.get(id).portNumber;
}

void MidiPortRegistry::clear()
{
    entries.clear();
}

} // namespace MakeASound
