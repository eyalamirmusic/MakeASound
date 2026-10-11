#pragma once

#include "../Core/Editor.h"

#include <eacp/Graphics/View/View.h>

namespace MakeASound
{

// The bottom-right corner handle an editor draws for a host whose window has
// none. Add it as the editor view's last child, so it sits above everything,
// and place it at cornerOf(bounds) in the editor's layout. It draws and takes
// the mouse only while editor.canRequestResize(). Message thread.
class ResizeGrip final : public eacp::Graphics::View
{
public:
    explicit ResizeGrip(Editor& editorToUse);

    static constexpr auto size = 18.f;

    static eacp::Graphics::Rect cornerOf(const eacp::Graphics::Rect& area);

    // Whether an editor should show one: it can ask for a size, and its host
    // has no grip of its own.
    static bool isWantedBy(const Editor& editor);

    void paint(eacp::Graphics::Context& context) override;
    View* hitTest(const eacp::Graphics::Point& point) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseDragged(const eacp::Graphics::MouseEvent& event) override;

private:
    Editor& editor;

    // Each drag step is measured from here, not from the last request: asking
    // is not getting, so accumulating requests would drift off the pointer.
    EditorSize sizeAtDragStart;
};

} // namespace MakeASound
