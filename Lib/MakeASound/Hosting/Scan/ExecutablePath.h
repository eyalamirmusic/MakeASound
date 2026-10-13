#pragma once

#include <string>

namespace MakeASound::Hosting
{

// This process's own executable, which is what a scan relaunches as its worker
// by default. Prefers eacp's recorded argv[0] (set by eacp::Apps::setCommandLineArgs)
// and falls back to the platform's answer, so a console tool that never records
// it still works.
//
// Made absolute, because a child inherits this process's working directory but
// the caller may have been launched by a relative path from somewhere else.
// The exception is an argv[0] with no directory at all: the shell found the
// program on PATH, and absolutising that against the working directory names a
// file that is almost never there. Left bare, the spawn repeats the shell's
// PATH lookup.
std::string currentExecutablePath();

// The platform's own answer, one TU per platform (ExecutablePath-<os>.cpp).
// Empty when it cannot tell.
std::string executablePathFromPlatform();

} // namespace MakeASound::Hosting
