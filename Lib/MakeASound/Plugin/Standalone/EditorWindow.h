#pragma once

#include "../Core/Editor.h"

#include <eacp/Graphics/Window/Window.h>

#include <string>

namespace MakeASound::Standalone
{

// The editor window's options from the editor's size policy: opened at the
// constrained initial size, every size the window takes put through
// EditorConstraints::constrain, and no fullscreen for a ratio-locked editor.
eacp::Graphics::WindowOptions editorWindowOptions(const Editor& editor,
                                                  std::string title);

void removeResizable(eacp::Graphics::WindowOptions& options);

} // namespace MakeASound::Standalone
