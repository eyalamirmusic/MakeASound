#include "ResizeGrip.h"

#include <algorithm>
#include <cmath>

namespace MakeASound
{
using namespace eacp::Graphics;

ResizeGrip::ResizeGrip(Editor& editorToUse)
    : editor(editorToUse)
{
    setHandlesMouseEvents();
}

Rect ResizeGrip::cornerOf(const Rect& area)
{
    return {area.x + area.w - size, area.y + area.h - size, size, size};
}

bool ResizeGrip::isWantedBy(const Editor& editor)
{
    return editor.canRequestResize() && !editor.host()->drawsResizeHandle();
}

void ResizeGrip::paint(Context& context)
{
    if (!editor.canRequestResize())
        return;

    auto area = getLocalBounds();
    auto span = std::min(area.w, area.h);
    auto right = area.x + area.w - 1.f;
    auto bottom = area.y + area.h - 1.f;

    context.setLineWidth(1.f);

    // Dark under light, so the diagonals read over a bright page and a dark one.
    for (auto shadow: {true, false})
    {
        auto offset = shadow ? 1.f : 0.f;
        context.setColor(shadow ? Color::black(0.35f) : Color::white(0.55f));

        for (auto line = 1; line <= 3; ++line)
        {
            auto inset = span * static_cast<float>(line) / 4.f;

            context.drawLine({right + 1.f - inset + offset, bottom + offset},
                             {right + offset, bottom + 1.f - inset + offset});
        }
    }
}

View* ResizeGrip::hitTest(const Point& point)
{
    if (!editor.canRequestResize())
        return nullptr;

    return View::hitTest(point);
}

void ResizeGrip::mouseDown(const MouseEvent&)
{
    auto* parent = getParent();

    if (parent == nullptr)
        return;

    auto area = parent->getLocalBounds();
    sizeAtDragStart = {static_cast<int>(std::lround(area.w)),
                       static_cast<int>(std::lround(area.h))};
}

void ResizeGrip::mouseDragged(const MouseEvent& event)
{
    // Both points are in this view's current coordinates, so their difference is
    // the pointer's travel since mouseDown however far the grip moved since.
    auto travelled = event.pos - event.downPos;

    editor.requestResize(
        {sizeAtDragStart.width + static_cast<int>(std::lround(travelled.x)),
         sizeAtDragStart.height + static_cast<int>(std::lround(travelled.y))});
}

} // namespace MakeASound
