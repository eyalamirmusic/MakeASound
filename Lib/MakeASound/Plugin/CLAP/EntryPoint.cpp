// Compiled into each <Name>-CLAP module by makeasound_add_plugin, never into the
// static target: the plugin's own TU defines describeModule(), and the bundle id
// the plugin ids derive from arrives as MAKEASOUND_CLAP_BUNDLE_ID.
#include "Factory.h"
#include "Gui.h"
#include "../Realtime/MessageThread.h"

#include <clap/entry.h>

#include <cstring>

std::string_view MakeASound::CLAP::moduleBundleId() noexcept
{
    return MAKEASOUND_CLAP_BUNDLE_ID;
}

namespace
{
// init and deinit may come more than once each, so only the last deinit
// releases anything. Never two at once, by the entry's contract.
int initCount = 0;

bool entryInit(const char*) noexcept
{
    ++initCount;
    return true;
}

// The host unloads the module after the last one: the views' drawing devices,
// then the message thread's window, scheduler and queue go while our code is
// still mapped, as VST3's ModuleTerminator does (see VST3/EntryPoint.cpp).
void entryDeinit() noexcept
{
    if (initCount <= 0 || --initCount > 0)
        return;

    try
    {
        MakeASound::CLAP::releaseViewDevices();
    }
    catch (...)
    {
    }

    MakeASound::releaseHostMessageThread();
}

const void* entryFactory(const char* id) noexcept
{
    if (id == nullptr || std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) != 0)
        return nullptr;

    try
    {
        static const auto module = MakeASound::describeModule();
        static const auto factory =
            MakeASound::CLAP::Factory(module, MakeASound::CLAP::moduleBundleId());
        return factory.get();
    }
    catch (...)
    {
        return nullptr;
    }
}
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryFactory};
