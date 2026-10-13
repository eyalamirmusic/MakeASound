#pragma once

// MakeASoundHosting: scanning and loading third-party plugins. See
// Docs/Hosting.md and Docs/PluginScanning.md.

#include "PluginDescription.h"
#include "PluginCache.h"
#include "PluginFilter.h"
#include "HostFormat.h"
#include "FormatManager.h"
#include "Scan/ExecutablePath.h"
#include "Scan/Scanner.h"
#include "Scan/ScanWorker.h"
#include "Scan/ScanCoordinator.h"
#include "Scan/ScanSession.h"
#include "VST3/VST3Format.h"
