#pragma once

/** @file Choosing the standalone's editor and the window options for it. */

#include "../Core/Editor.h"
#include "../Core/Plugin.h"

#include <eacp/Graphics/Window/Window.h>

#include <string>

namespace MakeASound::Standalone
{

/// The editor the window shows: createEditor()'s, or GenericEditor when that is
/// null or the plugin's hasEditor() is false (createEditor() then never asked).
/// A standalone has no host page to fall back on, so it always shows a window,
/// as JUCE's standalone shows its generic editor for a processor with none.
OwningPointer<Editor> createWindowEditor(Plugin& plugin);

/// The editor window's options from the editor's size policy: opened at the
/// constrained initial size, every size the window takes put through
/// EditorConstraints::constrain, and no fullscreen for a ratio-locked editor.
eacp::Graphics::WindowOptions editorWindowOptions(const Editor& editor,
                                                  std::string title);

/// Takes WindowFlags::Resizable out of the options' flags.
void removeResizable(eacp::Graphics::WindowOptions& options);

} // namespace MakeASound::Standalone
