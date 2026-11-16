#pragma once

// CLAP-only: includes the CLAP headers, through CLAPHostedPlugin.h.

#include "../HostedEditor.h"
#include "CLAPHostedPlugin.h"

#include <functional>

namespace eacp::Graphics
{
class NativeChildSurface;
}

namespace MakeASound::Hosting
{

// A CLAP plugin's clap.gui, embedded (never floating) in an eacp view through
// the platform's window API: cocoa on macOS, win32 on Windows, x11 on Linux. On
// the main thread throughout, in the spec's order:
//
//   create()      is_api_supported(api, false), create(api, false), and where
//                 the API speaks physical pixels set_scale
//   onAttached()  set_scale again if the parent's differs, get_size, set_parent,
//                 then set_size if the host settled on another size, and show
//   onRemoved()   hide, destroy; attaching again creates the GUI anew, unless
//                 another editor of the plugin holds the GUI by then
//
// The editor holds the plugin's one GUI (and is its CLAPGuiListener) from
// create(), or a later attach, until the GUI is destroyed: a removed editor
// blocks no other, and what the plugin asked of one GUI never reaches the next.
//
// A GUI the plugin lost (closed with was_destroyed) takes no call from then on
// but its destroy(), which the next service runs, never inside one of the
// plugin's own calls (closed may come from its timer, set_parent or show); the
// view stays empty until attached again. Nothing reaches the GUI once it is
// destroyed, nor the plugin once it is: a plugin destroyed while its editor
// lives destroys the GUI first, and the editor then shows nothing.
//
// Sizes are points to the host. Cocoa's are points already; win32's and x11's
// are physical pixels, converted at the scale the plugin was told (the child
// window's DPI on Windows, the surface's backing scale on Linux).
// constraints() reads can_resize and get_resize_hints (an axis the hints lock
// stays put in constrainSize(), a ratio becomes aspectRatio), constrainSize()
// asks adjust_size, and a host-driven resize reaches a resizable plugin as
// set_size (never a fixed-size one, nor one known to have that size: compared in
// points, so a fractional scale's rounding sends no echo). The plugin's
// request_resize becomes Editor::requestOwnResize, as VST3's resizeView does,
// honoured whether or not the user may drag it.
class HostedCLAPEditor : public HostedEditor
{
public:
    // Null when the plugin offers no clap.gui, cannot embed in this platform's
    // window API, or will not create its GUI. The spec allows one GUI per
    // instance, so a second editor of the same plugin is null while the first
    // holds its GUI (from its creation until it is removed), and its create()
    // is never called.
    static OwningPointer<Editor> create(CLAPHostedPlugin& plugin);

    // Use create(), which answers null where this one's GUI was not created.
    explicit HostedCLAPEditor(CLAPHostedPlugin& plugin);
    ~HostedCLAPEditor() override;

    eacp::Graphics::View& view() override;
    EditorSize initialSize() const override;
    EditorConstraints constraints() const override;
    EditorSize constrainSize(EditorSize proposed) const override;

    void onAttached() override;
    void onRemoved() override;

    // Whether the plugin's GUI exists (created, not yet destroyed), and whether
    // it is parented into the view.
    bool isGuiCreated() const noexcept;
    bool isGuiAttached() const noexcept;

    // The plugin said its resize hints changed (resize_hints_changed):
    // constraints() already answers the new ones, for a host whose window
    // keeps its own copy.
    std::function<void()> onConstraintsChanged;

    // Tests only: the scale every hosted CLAP editor converts sizes at from now
    // on, in place of the platform's (set_scale still goes only where the
    // platform uses it); 0 restores the platform's. Main thread.
    static void overrideScaleForTesting(float scale) noexcept;

private:
    struct Impl;
    OwningPointer<Impl> impl;
};

// What differs per platform (CLAPEditorPlatform-<os>.cpp).
namespace CLAPEditorPlatform
{
// CLAP_WINDOW_API_COCOA, _WIN32 or _X11.
const char* windowApi() noexcept;

// The clap_window for the surface's native handle (an X11 id widened into the
// pointer on Linux).
clap_window windowFor(void* nativeParent) noexcept;

// Whether the API speaks physical pixels, so the plugin is told the scale
// through set_scale: not cocoa's, which the spec says never to scale.
bool usesScale() noexcept;

// Physical pixels per point for the native window: 1 on macOS, the window's DPI
// on Windows, the surface's backing scale on Linux. Either may be null before
// the view is in a window.
float scaleFor(eacp::Graphics::NativeChildSurface* surface,
               void* nativeParent) noexcept;

// Calls onChange when the scale moves without a resize (the surface's
// onBackingScaleChanged on Windows and Linux); nothing on macOS, whose points
// never change with it.
void watchScale(eacp::Graphics::NativeChildSurface& surface,
                std::function<void()> onChange);
} // namespace CLAPEditorPlatform

} // namespace MakeASound::Hosting
