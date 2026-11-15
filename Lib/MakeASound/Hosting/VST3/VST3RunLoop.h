#pragma once

// VST3-only, after VST3Common.h.

#include <pluginterfaces/base/funknown.h>

namespace MakeASound::Hosting
{

// The run loop a Linux plugin registers its descriptors and timers with
// (Steinberg::Linux::IRunLoop, over eacp's message loop). A load answers it from
// the host context and an editor from its IPlugFrame. Null on every other
// platform, where VST3 names none (VST3RunLoop-<os>.cpp); nothing outside the
// Linux TU names the interface, whose iid the SDK defines only there.
class VST3RunLoop : public Steinberg::FUnknown
{
public:
    static Steinberg::IPtr<VST3RunLoop> create();

    // For a queryInterface that lends the loop: true, with `obj` set and a
    // reference added, when `_iid` asks for the run loop.
    virtual bool answer(const Steinberg::TUID _iid, void** obj) noexcept = 0;

    // Drops every handler and timer still registered, for when whatever lent
    // the loop goes away before the plugin unregisters (an editor closing, a
    // module about to be unloaded).
    virtual void clear() noexcept = 0;
};

} // namespace MakeASound::Hosting
