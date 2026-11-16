#pragma once

#include "../Core/Editor.h"
#include "../../Common/Common.h"

namespace eacp::Graphics
{
class EmbeddedView;
struct KeyEvent;
} // namespace eacp::Graphics

namespace MakeASound
{

// The format-neutral half of a DAW's window onto a plugin's Editor, which the
// VST3 PlugView and the CLAP gui share: it owns the editor, makes `host` its
// EditorHost for its lifetime, embeds its view through eacp in the native
// parent a host hands over, and converts between the editor's points and the
// host's units.
//
// A host's unit is a point on macOS, where AppKit scales the backing store
// itself, and a physical pixel on Windows and X11: unitScale() is how many of
// them make a point. A host may name that scale (setHostScale: VST3's content
// scale, CLAP's set_scale); one that never does leaves, on Windows, the parent
// window's DPI, and on Linux 1. The platform's half is in
// EmbeddedEditor-<platform>.cpp: the scale, how it reaches the embedded view,
// and what becomes of a key the editor did not consume. Message thread.
class EmbeddedEditor
{
public:
    EmbeddedEditor(OwningPointer<Editor> editorToShow, EditorHost& host);
    ~EmbeddedEditor();

    EmbeddedEditor(const EmbeddedEditor&) = delete;
    EmbeddedEditor& operator=(const EmbeddedEditor&) = delete;

    Editor& editor() noexcept { return *shown; }
    EditorConstraints constraints() const;
    bool resizable() const;

    // Host units per point.
    float unitScale() const noexcept;

    EditorSize toUnits(EditorSize points) const noexcept;
    EditorSize toPoints(EditorSize units) const noexcept;

    // The size a host opens the editor at, constraints().constrain(
    // initialSize()), in host units.
    EditorSize openingSize() const;

    // A size in host units put through the editor's constraints, in host units.
    EditorSize constrain(EditorSize units) const;

    // The scale the host names, which from then on wins over the platform's.
    // Kept, but read by nothing, on macOS.
    void setHostScale(float pixelsPerPoint) noexcept;
    bool hostNamedScale() const noexcept { return hostScaled; }

    // Embeds the editor's view in `parent` (an NSView*, an HWND, an X11 window
    // id widened into the pointer) at `units`, then tells the editor it is
    // attached; detach() tells it it is removed first. Attaching again detaches
    // from the old parent first.
    void attach(void* parent, EditorSize units);
    void detach();
    bool isAttached() const noexcept { return embedded != nullptr; }

    // The handle attach() was given, null while detached.
    void* parent() const noexcept { return parentHandle; }

    // The embedded surface follows the host's size, in host units.
    void setSize(EditorSize units);

    // Shows or hides the surface without tearing it down; false while detached.
    bool setVisible(bool shouldBeVisible);

    // EditorHost::unhandledKey for a format's host: on Windows posted to the
    // parent window while attached, elsewhere left to the view.
    bool unhandledKey(const eacp::Graphics::KeyEvent& key);

private:
    // Hands the scale to the embedded view where the platform leaves that to us.
    void applyScale() noexcept;

    OwningPointer<Editor> shown;
    OwningPointer<eacp::Graphics::EmbeddedView> embedded;
    void* parentHandle = nullptr;
    float scale = 1.f;
    bool hostScaled = false;
};

// Gives back the drawing devices the editors brought up for the whole process
// (eacp::Graphics::releaseSharedDevices), at a module's exit while its code is
// still mapped.
void releaseEmbeddedEditorDevices();

} // namespace MakeASound
