#pragma once

/** @file
    Validates a built plugin with pluginval, auval or the AAX validator (Tracktion's
    pluginval for a .vst3, Apple's auval for a .component, Avid's aaxval for an
    .aaxplugin), run from C++: what the PluginValidator tool and the env-gated test
    both drive.
*/

#include "../../Common/Common.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Time.h>
#include <eacp/Network/OnlineResource/OnlineResource.h>

#include <string>

namespace MakeASound::Pluginval
{
/// How strictly and how long to validate, and where the validators live.
struct Options
{
    int strictness = 10;
    bool guiTests = true;
    eacp::Time::MS timeout {120000};
    std::string version = "v1.0.4";
    int stress = 0;
    eacp::FilePath directory = eacp::OnlineResource::defaultDirectory();

    /// The unpacked AAX Validator folder (the one holding CommandLineTools/dsh).
    /// Avid ships it under NDA only, so it is always the user's own copy.
    eacp::FilePath aaxTools;

    /// How long dsh may print nothing before it counts as hung and is killed: one
    /// aaxval test prints its result only when done.
    eacp::Time::MS aaxStallTimeout {10 * 60 * 1000};
};

/// The pluginval binary for this machine, downloaded on the first call and found on
/// disk after. Throws std::runtime_error when it cannot be had, Windows ARM64 included
/// (Tracktion ships no build for it).
eacp::FilePath fetch(const Options& options = {});

/// One validation run; `seconds` is its wall-clock duration.
struct Result
{
    eacp::FilePath bundle;
    bool passed = false;
    int exitCode = -1;
    std::string log;
    double seconds = 0;
};

/// By the bundle's extension alone: .component and .aaxplugin.
bool isAudioUnit(const eacp::FilePath& bundle);
bool isAAX(const eacp::FilePath& bundle);

/// A .component ignores `pluginval`: it is installed into the user's Components
/// folder and each of its AudioComponents entries runs through auval -strict, on
/// macOS only. An .aaxplugin ignores it too and runs every aaxval test in
/// Options::aaxTools' DigiShell, on macOS only.
Result validate(const eacp::FilePath& pluginval,
                const eacp::FilePath& bundle,
                const Options& options = {});

/// Every *.vst3, *.component and *.aaxplugin directly under `directory`, sorted by
/// name.
Vector<eacp::FilePath> findBundles(const eacp::FilePath& directory);
} // namespace MakeASound::Pluginval
