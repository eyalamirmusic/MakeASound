#pragma once

#include <MakeASound/Hosting/Scan/ScanSession.h>

// Declared only: a front end that calls addScanOptions() includes
// <eacp/Core/App/Args.h> and links eacp-core itself.
namespace eacp::Args
{
class Parser;
}

namespace MakeASound::ScanProtocol
{

using Hosting::ScanSessionOptions;

// The command line of a sweep, for a console front end built on eacp's Args.
// Declares the standard scan options on `parser`, each bound straight onto a
// field of `options`, so the parsed command line IS the session's options:
//
//   --output <file>        cachePath
//   --folder <dir>         searchFolders (repeatable)
//   --no-default-folders   useDefaultFolders = false
//   --format <VST3|AU>     formats (repeatable, any case)
//   --force                forceRescan
//   --jobs <n>             jobs, at least 1
//   --timeout <ms>         timeoutMs, at least 0
//
// `options` must outlive the parse, and its values before it are the defaults
// the help text shows. The worker command and the scan mode are the front end's
// to set.
void addScanOptions(eacp::Args::Parser& parser, ScanSessionOptions& options);

} // namespace MakeASound::ScanProtocol
