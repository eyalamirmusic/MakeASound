#include "VST3Common.h"
#include "PlugView.h"
#include "PlugViewFactory.h"
#include "../UI/GenericEditor.h"

#include <eacp/Graphics/View/View.h>
#include <eacp/Graphics/Window/EmbeddedView.h>

#include <cmath>
#include <cstring>

namespace MakeASound::VST3
{

namespace
{
bool isNativeViewType(FIDString type)
{
    return type != nullptr && std::strcmp(type, PlugView::nativeViewType()) == 0;
}

int scaleBy(int value, float factor)
{
    return static_cast<int>(std::lround(static_cast<float>(value) * factor));
}
} // namespace

Steinberg::IPlugView* createPlugView(FUnknown& owner, Plugin& plugin)
{
    auto editor = plugin.createEditor();

    if (!editor)
        editor = EA::makeOwned<GenericEditor>(plugin);

    return new PlugView(Steinberg::IPtr<FUnknown>(&owner), std::move(editor));
}

PlugView::PlugView(Steinberg::IPtr<FUnknown> ownerToKeep,
                   OwningPointer<Editor> editorToShow)
    : owner(std::move(ownerToKeep))
    , editor(std::move(editorToShow))
{
    setRect(rectFor(editor->initialSize()));
}

PlugView::~PlugView()
{
    if (embedded)
        removedFromParent();
}

Steinberg::ViewRect PlugView::rectFor(EditorSize size) const noexcept
{
    auto factor = rectScale();
    return {0, 0, scaleBy(size.width, factor), scaleBy(size.height, factor)};
}

tresult PLUGIN_API PlugView::isPlatformTypeSupported(FIDString type)
{
    return isNativeViewType(type) ? Steinberg::kResultTrue : Steinberg::kResultFalse;
}

tresult PLUGIN_API PlugView::attached(void* parent, FIDString type)
{
    if (!isNativeViewType(type))
        return Steinberg::kResultFalse;

    return CPluginView::attached(parent, type);
}

void PlugView::attachedToParent()
{
    auto factor = rectScale();
    auto options = eacp::Graphics::EmbeddedViewOptions {
        scaleBy(rect.getWidth(), 1.f / factor),
        scaleBy(rect.getHeight(), 1.f / factor)};

    embedded = EA::makeOwned<eacp::Graphics::EmbeddedView>(systemWindow, options);
    applyContentScale();
    embedded->setContentView(editor->view());
    editor->onAttached();
}

void PlugView::removedFromParent()
{
    editor->onRemoved();
    embedded.reset();
}

tresult PLUGIN_API PlugView::getSize(Steinberg::ViewRect* size)
{
    if (size == nullptr)
        return Steinberg::kInvalidArgument;

    if (!sizedByHost)
        setRect(rectFor(editor->initialSize()));

    return CPluginView::getSize(size);
}

tresult PLUGIN_API PlugView::onSize(Steinberg::ViewRect* newSize)
{
    if (newSize == nullptr)
        return Steinberg::kInvalidArgument;

    sizedByHost = true;
    auto result = CPluginView::onSize(newSize);

    if (embedded)
    {
        auto factor = rectScale();
        embedded->setSize(scaleBy(newSize->getWidth(), 1.f / factor),
                          scaleBy(newSize->getHeight(), 1.f / factor));
    }

    return result;
}

tresult PLUGIN_API PlugView::canResize()
{
    return editor->isResizable() ? Steinberg::kResultTrue : Steinberg::kResultFalse;
}

tresult PLUGIN_API PlugView::checkSizeConstraint(Steinberg::ViewRect* rectToCheck)
{
    if (editor->isResizable())
        return Steinberg::kResultTrue;

    return CPluginView::checkSizeConstraint(rectToCheck);
}

tresult PLUGIN_API PlugView::setFrame(Steinberg::IPlugFrame* frame)
{
    auto result = CPluginView::setFrame(frame);
    frameLoop = attachHostRunLoop(frame);
    return result;
}

tresult PLUGIN_API PlugView::setContentScaleFactor(ScaleFactor factor)
{
    if (!(factor > 0.f))
        return Steinberg::kInvalidArgument;

    scale = factor;
    applyContentScale();
    return Steinberg::kResultOk;
}

} // namespace MakeASound::VST3
