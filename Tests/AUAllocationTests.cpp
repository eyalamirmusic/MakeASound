// The AU adapter's audio-thread paths under the allocation ban, driven through
// AudioToolbox's C API as a host drives them: whole AudioUnitRender calls with
// host parameter writes, MIDI in and echoed out, over varying slice sizes and a
// re-initialization, and the same while every parameter is held.

#include "AllocationProbe.h"
#include "AUTestHost.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>

using namespace nano;
using namespace TestPlugins;
using namespace AUHost;
using Probe::allocationsIn;

namespace
{
template <typename P>
std::vector<AudioUnitParameterID> exposedIds(UnitHost<P>& host)
{
    auto ids = std::vector<AudioUnitParameterID> {};
    const auto& list = host.plugin().parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (list.isHostExposed(i))
            ids.push_back(list.entry(i).hostId);

    return ids;
}

// Everything a busy host slice carries: a write to every parameter, notes, a
// controller and a bend at their frames, then the render.
template <typename P>
OSStatus busyBlock(UnitHost<P>& host,
                   const std::vector<AudioUnitParameterID>& ids,
                   int block,
                   UInt32 frames)
{
    const auto& list = host.plugin().parameters();
    auto normalized = block % 2 == 0 ? 0.3f : 0.7f;

    for (auto id: ids)
        host.hostWrites(id, list[list.indexOfHostId(id)].toPlain(normalized));

    if (host.hasMidiOut())
    {
        auto last = std::max<UInt32>(frames, 1) - 1;
        MusicDeviceMIDIEvent(host.unit, 0x90, 60, 100, 0);
        MusicDeviceMIDIEvent(host.unit, 0xB0, 1, 64, last / 3);
        MusicDeviceMIDIEvent(host.unit, 0xE0, 0, 80, last * 2 / 3);
        MusicDeviceMIDIEvent(host.unit, 0x80, 60, 0, last);
    }

    host.sent.clear();
    return host.render(frames);
}

template <typename P>
int allocationsOverBlocks(UnitHost<P>& host, UInt32 maxFrames)
{
    auto ids = exposedIds(host);
    auto count = 0;
    auto block = 0;
    auto status = OSStatus {noErr};

    status |= busyBlock(host, ids, block++, maxFrames);

    for (auto size: {64u, 17u, 1u, 63u, 0u})
    {
        auto frames = std::min(size, maxFrames);
        count +=
            allocationsIn([&] { status |= busyBlock(host, ids, block++, frames); });
    }

    check(status == noErr);
    return count;
}

template <typename P>
void checkRenderIsOffTheHeap(FourCC code)
{
    auto host = UnitHost<P>(code);

    check(allocationsOverBlocks(host, maxBlock) == 0);

    check(host.initialize(44100.0, 32) == noErr);
    check(allocationsOverBlocks(host, 32) == 0);
}

auto tEffectRender = test("Allocations/auEffectRenderIsOffTheHeap") = []
{
    checkRenderIsOffTheHeap<GainPlugin>("Gain");

    auto host = UnitHost<GainPlugin>("Gain");
    allocationsOverBlocks(host, maxBlock);
    check(host.plugin().processed > 0);
};

auto tInstrumentRender = test("Allocations/auInstrumentRenderIsOffTheHeap") = []
{
    checkRenderIsOffTheHeap<SynthPlugin>("Echo");

    // Packets at one timestamp merge, so the count is read off a full slice.
    auto host = UnitHost<SynthPlugin>("Echo");
    check(busyBlock(host, exposedIds(host), 0, maxBlock) == noErr);
    check(host.plugin().numEvents == 4);
    check(host.sent.size() == 4);
};

template <typename P>
void checkHeldRenderIsOffTheHeap(FourCC code)
{
    auto host = UnitHost<P>(code);
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();

    check(allocationsOverBlocks(host, maxBlock) == 0);

    // The gesture is the editor's, on the message thread, but it is the path a
    // drag takes many times a second.
    check(allocationsIn(
              [&]
              {
                  for (auto i = 0; i < numParams; ++i)
                  {
                      listener.beginParameterEdit(i);
                      listener.performParameterEdit(i, 0.5f);
                  }
              })
          == 0);

    check(allocationsOverBlocks(host, maxBlock) == 0);

    check(allocationsIn(
              [&]
              {
                  for (auto i = 0; i < numParams; ++i)
                      listener.endParameterEdit(i);
              })
          == 0);

    check(allocationsOverBlocks(host, maxBlock) == 0);
}

auto tHeldRender = test("Allocations/auHeldParameterRenderIsOffTheHeap") = []
{
    checkHeldRenderIsOffTheHeap<GainPlugin>("Gain");
    checkHeldRenderIsOffTheHeap<SynthPlugin>("Echo");
};
} // namespace
