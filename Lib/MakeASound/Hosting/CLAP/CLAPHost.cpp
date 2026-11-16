#include "CLAPHost.h"

#include <clap/version.h>

namespace MakeASound::Hosting
{

CLAPHost::CLAPHost(std::string nameToUse)
    : name(std::move(nameToUse))
{
    host.clap_version = CLAP_VERSION;
    host.host_data = this;
    host.name = name.c_str();
    host.vendor = "MakeASound";
    host.url = "";
    host.version = "1.0";
    host.get_extension = [](const clap_host*, const char*) -> const void*
    { return nullptr; };
    host.request_restart = [](const clap_host*) {};
    host.request_process = [](const clap_host*) {};
    host.request_callback = [](const clap_host*) {};
}

} // namespace MakeASound::Hosting
