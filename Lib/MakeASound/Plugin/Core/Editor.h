#pragma once

namespace eacp::Graphics
{
class View;
struct KeyEvent;
} // namespace eacp::Graphics

namespace MakeASound
{

struct EditorSize
{
    int width = 400;
    int height = 300;
};

// An editor's size policy, in points. Only the proportion of aspectRatio is
// read, so {3, 2} and {600, 400} mean the same; a zero side locks nothing.
struct EditorConstraints
{
    bool resizable = false;
    EditorSize minimum {1, 1};
    EditorSize maximum {16384, 16384};
    EditorSize aspectRatio {0, 0};

    // The one rule every format applies to a proposed size: each side clamped
    // to the bounds, and with a ratio the proposal projected onto it, the
    // minimum winning when the bounds and the ratio admit no size.
    EditorSize constrain(EditorSize proposed) const noexcept;
};

// Whatever shows an editor: the standalone's window, a VST3 PlugView, an AU
// Cocoa view. Message thread.
class EditorHost
{
public:
    virtual ~EditorHost() = default;

    // True when the host took the size.
    virtual bool resizeEditor(EditorSize size) = 0;

    // Whether the host's window has a resize grip of its own.
    virtual bool drawsResizeHandle() const = 0;

    // A key the editor's view did not consume, from Editor::forwardUnhandledKey.
    // True when the host claimed it: the standalone plays it on the typing
    // keyboard, and the VST3 view on Windows posts it to the host's window, which
    // the key would not otherwise reach (eacp's fallback re-sends it to the
    // editor's own window). False, the default, leaves the key to the view, whose
    // own route goes on to whatever hosts it: on macOS the responder chain
    // already reaches the DAW's window.
    virtual bool unhandledKey(const eacp::Graphics::KeyEvent& key)
    {
        (void) key;
        return false;
    }
};

// What a plugin shows: one eacp view the standalone puts in its window and a
// DAW adapter embeds in the host's. Only the forward declaration of the view
// reaches this header; whoever hosts or builds an editor links eacp's GUI tier.
//
// Every format calls setHost(this) before onAttached() and setHost(nullptr)
// after onRemoved(), so host() is valid from onAttached() on. The host is the
// format's, never owned.
class Editor
{
public:
    virtual ~Editor() = default;

    Editor() = default;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    // A stable reference for the editor's lifetime, built on first call at the
    // latest. Message thread.
    virtual eacp::Graphics::View& view() = 0;

    // A host opens the editor at constraints().constrain(initialSize()).
    virtual EditorSize initialSize() const { return {}; }
    virtual EditorConstraints constraints() const { return {}; }

    // The view went into a window or an embedded host view, or came out of
    // one. Message thread.
    virtual void onAttached() {}
    virtual void onRemoved() {}

    void setHost(EditorHost* hostToUse) noexcept { editorHost = hostToUse; }
    EditorHost* host() const noexcept { return editorHost; }

    bool canRequestResize() const;

    // Constrains the size and hands it to the host; false when there is no
    // host, the editor is not resizable or the host refused.
    bool requestResize(EditorSize size);

    // Hands the host a key the editor's view did not consume, so a DAW's
    // shortcuts (spacebar transport) and the standalone's typing keyboard work
    // while the editor has focus. True when the host claimed it; false without a
    // host, and the caller then sends the key on its own way. A web page's
    // editor wires its WebView here, with Options::forwardUnhandledKeys on:
    //
    //     webView.onUnhandledKeyEvent = [this](const eacp::Graphics::KeyEvent& key)
    //     { return forwardUnhandledKey(key); };
    //
    // Message thread.
    bool forwardUnhandledKey(const eacp::Graphics::KeyEvent& key);

private:
    EditorHost* editorHost = nullptr;
};

} // namespace MakeASound
