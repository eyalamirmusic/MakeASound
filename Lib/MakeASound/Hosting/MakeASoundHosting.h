#pragma once

/** @file
    Scanning and loading third-party plugins (MakeASoundHosting, namespace
    MakeASound::Hosting). macOS and Windows only, and only when the VST3 SDK's
    hosting sources are present. Formats: VST3 everywhere, Audio Units on macOS
    (AU/AUFormat.h, included by name where used). See Docs/Hosting.md and
    Docs/PluginScanning.md.

    Entry points: FormatManager (addDefaultFormats(), then load() a
    PluginDescription into a HostedPlugin), ScanSession for a whole sweep into a
    PluginCache, ScanCoordinator for a list of targets.

    - Scanning runs plugin code: use ScanMode::Subprocess, the default, so a
      crashing plugin takes down a worker, not the host.
    - Scans, loads and editors are message-thread work; a HostedPlugin's
      process() runs on the audio thread like any Plugin.
    - Errors come back as values (LoadResult::error, a failed ModuleScan).
    - Editors live in the separate MakeASoundHostedEditor library (HostedEditor.h).
*/

#include "PluginDescription.h"
#include "PluginCache.h"
#include "PluginFilter.h"
#include "HostedPlugin.h"
#include "HostFormat.h"
#include "FormatManager.h"
#include "JuceHostCompat.h"
#include "Scan/ExecutablePath.h"
#include "Scan/Scanner.h"
#include "Scan/WorkerCommand.h"
#include "Scan/WorkerProcess.h"
#include "Scan/ScanCoordinator.h"
#include "Scan/ScanSession.h"
#include "VST3/VST3Format.h"

// AU/AUFormat.h is macOS only, so it is included by name where it is used.
