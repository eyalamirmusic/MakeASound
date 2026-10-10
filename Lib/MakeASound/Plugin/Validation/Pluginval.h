#pragma once

#include "../../Common/Common.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Time.h>
#include <eacp/Network/OnlineResource/OnlineResource.h>

#include <string>

// Tracktion's pluginval, fetched and run from C++: what the PluginValidator tool
// and the env-gated test both drive.
namespace MakeASound::Pluginval
{
struct Options
{
    int strictness = 10;
    bool guiTests = true;
    eacp::Time::MS timeout {120000};
    std::string version = "v1.0.4";
    eacp::FilePath directory = eacp::OnlineResource::defaultDirectory();
};

// The pluginval binary for this machine, downloaded on the first call and found on
// disk after. Throws std::runtime_error when it cannot be had, Windows ARM64 included
// (Tracktion ships no build for it).
eacp::FilePath fetch(const Options& options = {});

struct Result
{
    eacp::FilePath bundle;
    bool passed = false;
    int exitCode = -1;
    std::string log;
    double seconds = 0;
};

Result validate(const eacp::FilePath& pluginval,
                const eacp::FilePath& bundle,
                const Options& options = {});

// Every *.vst3 directly under `directory`, sorted by name.
Vector<eacp::FilePath> findBundles(const eacp::FilePath& directory);
} // namespace MakeASound::Pluginval
