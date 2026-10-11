#pragma once

namespace eacp::Graphics
{
class View;
}

namespace MakeASound
{

struct EditorSize
{
    int width = 400;
    int height = 300;
};

// What a plugin shows: one eacp view the standalone puts in its window and a
// DAW adapter embeds in the host's. Only the forward declaration of the view
// reaches this header; whoever hosts or builds an editor links eacp's GUI tier.
// Size policy beyond the initial size and host-driven resizing arrive later.
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

    virtual EditorSize initialSize() const { return {}; }
    virtual bool isResizable() const { return false; }

    // The view went into a window or an embedded host view, or came out of
    // one. Message thread.
    virtual void onAttached() {}
    virtual void onRemoved() {}
};

} // namespace MakeASound
