#pragma once

#include "../PluginDescription.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// Reads a worker's whole captured stdout into its scan; nullopt means no usable
// answer, which the host records as a crash.
using ScanResultDecoder =
    std::function<std::optional<ModuleScan>(std::string_view workerOutput)>;

// How to launch one scan worker and read what it answers. The library has no
// worker protocol of its own: the host builds the command line as
// `executable argumentPrefix... target` and reads the answer with `decode`, so
// whoever writes the worker chooses both. Apps/ScanProtocol is one such protocol.
struct WorkerCommand
{
    std::string executable;
    Vector<std::string> argumentPrefix;
    ScanResultDecoder decode;
};

} // namespace MakeASound::Hosting
