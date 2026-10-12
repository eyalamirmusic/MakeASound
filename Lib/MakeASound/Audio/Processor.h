#pragma once

#include "ProcessContext.h"

namespace MakeASound
{

// The format-neutral unit of audio work: something that is prepared for a spec
// and then handed one block at a time. An app runs one through Engine with a
// device behind it; a plugin is one with a host behind it.
class Processor
{
public:
    virtual ~Processor() = default;

    // What the host sizes the context to. Asked before prepare().
    virtual BusLayout getBusLayout() const { return BusLayout::stereoInOut(); }

    // Before the first process() and again whenever the spec changes. Allocate
    // here. Normally the host thread; Engine calls it from the audio thread when a
    // re-opened device turns up with a shape nobody prepared for.
    virtual void prepare(const ProcessSpec& spec) = 0;

    // Audio thread. Must not allocate, lock or block.
    virtual void process(ProcessContext& context) noexcept = 0;

    // Audio thread, after a gap in the stream (a restart, an xrun, a device
    // change): drop any state that spans blocks, keep what prepare() built.
    virtual void reset() noexcept {}
};

} // namespace MakeASound
