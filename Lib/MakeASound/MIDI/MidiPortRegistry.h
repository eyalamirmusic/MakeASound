#pragma once

#include "../Common/Common.h"

#include <string>

namespace MakeASound
{

// Ids have to outlive an enumeration: a host caches a MidiPortInfo and opens it
// later, and a hotplug renumbers every port that came after the one that moved. So
// an id is a slot in this registry, handed back to the same identity every time it
// is scanned, and the port number it sits at now is resolved through it at open
// time. The identity is whatever the platform offers as stable — a unique id, a
// client:port pair, a name.
class MidiPortRegistry
{
public:
    // Forgets the previous scan's port numbers; call once before a pass of idFor().
    void beginScan();

    // The slot this identity has held since it was first seen. A second port
    // wearing the same identity in one scan gets a slot of its own.
    int idFor(const std::string& identity, int portNumber);

    // Where that id sat in the last scan, or -1 once it is gone.
    int portNumberForId(int id) const;

    void clear();

private:
    struct Entry
    {
        std::string identity;
        int portNumber {-1};
        bool claimed {false};
    };

    Vector<Entry> entries;
};

} // namespace MakeASound
