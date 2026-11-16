#include "EmbeddedEditor.h"

#include <eacp/Graphics/Graphics/SharedDevices.h>
#include <eacp/Graphics/View/View.h>
#include <eacp/Graphics/Window/EmbeddedView.h>

#include <cmath>
#include <utility>

namespace MakeASound
{

namespace
{
int scaleBy(int value, float factor) noexcept
{
    return static_cast<int>(std::lround(static_cast<float>(value) * factor));
}
} // namespace

void releaseEmbeddedEditorDevices()
{
    eacp::Graphics::releaseSharedDevices();
}

EmbeddedEditor::EmbeddedEditor(OwningPointer<Editor> editorToShow, EditorHost& host)
    : shown(std::move(editorToShow))
{
    shown->setHost(&host);
}

EmbeddedEditor::~EmbeddedEditor()
{
    detach();
    shown->setHost(nullptr);
}

EditorConstraints EmbeddedEditor::constraints() const
{
    return shown->constraints();
}

bool EmbeddedEditor::resizable() const
{
    return constraints().resizable;
}

EditorSize EmbeddedEditor::toUnits(EditorSize points) const noexcept
{
    auto factor = unitScale();
    return {scaleBy(points.width, factor), scaleBy(points.height, factor)};
}

EditorSize EmbeddedEditor::toPoints(EditorSize units) const noexcept
{
    auto factor = 1.f / unitScale();
    return {scaleBy(units.width, factor), scaleBy(units.height, factor)};
}

EditorSize EmbeddedEditor::openingSize() const
{
    return toUnits(constraints().constrain(shown->initialSize()));
}

EditorSize EmbeddedEditor::constrain(EditorSize units) const
{
    return toUnits(constraints().constrain(toPoints(units)));
}

void EmbeddedEditor::setHostScale(float pixelsPerPoint) noexcept
{
    scale = pixelsPerPoint;
    hostScaled = true;
    applyScale();
}

// The handle is set before the size is converted: on Windows the parent's DPI
// is the scale of a host that named none.
void EmbeddedEditor::attach(void* parent, EditorSize units)
{
    if (embedded)
        detach();

    parentHandle = parent;

    auto points = toPoints(units);
    auto options = eacp::Graphics::EmbeddedViewOptions {points.width, points.height};

    embedded = EA::makeOwned<eacp::Graphics::EmbeddedView>(parent, options);
    applyScale();
    embedded->setContentView(shown->view());
    shown->onAttached();
}

void EmbeddedEditor::detach()
{
    parentHandle = nullptr;

    if (!embedded)
        return;

    shown->onRemoved();
    embedded.reset();
}

void EmbeddedEditor::setSize(EditorSize units)
{
    if (!embedded)
        return;

    auto points = toPoints(units);
    embedded->setSize(points.width, points.height);
}

bool EmbeddedEditor::setVisible(bool shouldBeVisible)
{
    if (!embedded)
        return false;

    embedded->setVisible(shouldBeVisible);
    return true;
}

} // namespace MakeASound
