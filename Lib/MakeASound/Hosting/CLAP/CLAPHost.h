#pragma once

#include <clap/host.h>

#include <string>

namespace MakeASound::Hosting
{

// The clap_host a plugin is created with: the host's name, and nothing else yet.
// get_extension answers no extension and every request_* is ignored, which is
// all a scan needs, since a scanned plugin is never activated. A plugin keeps
// the pointer, so this must outlive every plugin created with it. Internal to
// the CLAP format: it includes the CLAP headers.
class CLAPHost
{
public:
    explicit CLAPHost(std::string nameToUse);

    CLAPHost(const CLAPHost&) = delete;
    CLAPHost& operator=(const CLAPHost&) = delete;

    const clap_host* get() const noexcept { return &host; }

private:
    std::string name;
    clap_host host {};
};

} // namespace MakeASound::Hosting
