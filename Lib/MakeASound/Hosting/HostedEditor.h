#pragma once

// MakeASoundHostedEditor: a hosted plugin's own editor as a MakeASound Editor, an
// eacp view any window can embed. A separate library from MakeASoundHosting, so a
// scanner's worker never links graphics.

#include "HostedPlugin.h"
#include "../Plugin/Core/Editor.h"

namespace MakeASound::Hosting
{

// The plugin's view parented into an eacp view (macOS NSView, Windows child
// HWND). Sizes are points on every platform; a format that speaks pixels is
// converted inside. A resize the plugin asks for goes to the editor's host
// (Editor::requestOwnResize) even when constraints() says not resizable, which
// only keeps the user from dragging it; a size the host proposes is offered to
// the plugin first through constrainSize().
class HostedEditor : public Editor
{
public:
    // The nearest size to `proposed` the plugin accepts.
    virtual EditorSize constrainSize(EditorSize proposed) const = 0;
};

// The factory to pass as HostInfo::editors: the editor for each format this
// platform shows (VST3 on macOS and Windows, Audio Units on macOS), or null when
// the plugin has none. One TU per platform.
HostedEditorFactory hostedEditors();

} // namespace MakeASound::Hosting
