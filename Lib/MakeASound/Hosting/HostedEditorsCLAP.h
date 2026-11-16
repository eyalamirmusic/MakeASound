#pragma once

// hostedEditors()'s CLAP half, so each HostedEditors-<os>.cpp asks for a CLAP
// editor the same way whether or not the build hosts CLAP: through
// HostedEditorsCLAP-On.cpp, or -Off.cpp where CLAP is not built.

#include "HostedPlugin.h"

namespace MakeASound::Hosting
{

// The editor of a hosted CLAP plugin (a HostedCLAPEditor), or null for any other
// plugin, for a CLAP plugin with no GUI it can embed, and always where the build
// does not host CLAP.
OwningPointer<Editor> hostedCLAPEditor(HostedPlugin& plugin);

} // namespace MakeASound::Hosting
