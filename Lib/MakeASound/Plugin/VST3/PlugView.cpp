#include "VST3Common.h"
#include "PlugView.h"
#include "PlugViewFactory.h"
#include "../UI/GenericEditor.h"

#include <cstring>

namespace MakeASound::VST3
{

namespace
{
bool isNativeViewType(FIDString type)
{
    return type != nullptr && std::strcmp(type, PlugView::nativeViewType()) == 0;
}

EditorSize sizeOf(const Steinberg::ViewRect& rect) noexcept
{
    return {rect.getWidth(), rect.getHeight()};
}
} // namespace

Steinberg::IPlugView* createPlugView(FUnknown& owner, Plugin& plugin)
{
    auto editor = plugin.createEditor();

    if (!editor)
        editor = EA::makeOwned<GenericEditor>(plugin);

    return new PlugView(Steinberg::IPtr<FUnknown>(&owner), std::move(editor));
}

void releaseViewDevices()
{
    releaseEmbeddedEditorDevices();
}

PlugView::PlugView(Steinberg::IPtr<FUnknown> ownerToKeep,
                   OwningPointer<Editor> editorToShow)
    : owner(std::move(ownerToKeep))
    , embedding(std::move(editorToShow), *this)
{
    setRect(openingRect());
}

PlugView::~PlugView()
{
    embedding.detach();
}

Steinberg::ViewRect PlugView::rectFor(EditorSize units) const noexcept
{
    return {0, 0, units.width, units.height};
}

Steinberg::ViewRect PlugView::openingRect() const
{
    return rectFor(embedding.openingSize());
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
    embedding.attach(systemWindow, sizeOf(rect));
}

void PlugView::removedFromParent()
{
    embedding.detach();
}

tresult PLUGIN_API PlugView::getSize(Steinberg::ViewRect* size)
{
    if (size == nullptr)
        return Steinberg::kInvalidArgument;

    if (!sizedByHost)
        setRect(openingRect());

    return CPluginView::getSize(size);
}

tresult PLUGIN_API PlugView::onSize(Steinberg::ViewRect* newSize)
{
    if (newSize == nullptr)
        return Steinberg::kInvalidArgument;

    sizedByHost = true;
    ++sizeChanges;
    auto result = CPluginView::onSize(newSize);
    embedding.setSize(sizeOf(*newSize));
    return result;
}

tresult PLUGIN_API PlugView::canResize()
{
    return embedding.resizable() ? Steinberg::kResultTrue : Steinberg::kResultFalse;
}

tresult PLUGIN_API PlugView::checkSizeConstraint(Steinberg::ViewRect* rectToCheck)
{
    if (!embedding.resizable() || rectToCheck == nullptr)
        return CPluginView::checkSizeConstraint(rectToCheck);

    auto allowed = embedding.constrain(sizeOf(*rectToCheck));
    rectToCheck->right = rectToCheck->left + allowed.width;
    rectToCheck->bottom = rectToCheck->top + allowed.height;
    return Steinberg::kResultTrue;
}

// The SDK lets a host that accepts skip onSize when it judges the size unchanged,
// so an accepted request it did not answer is applied here, as JUCE does.
bool PlugView::resizeEditor(EditorSize size)
{
    if (!plugFrame)
        return false;

    auto requested = rectFor(embedding.toUnits(size));
    auto changesBefore = sizeChanges;

    if (plugFrame->resizeView(this, &requested) != Steinberg::kResultTrue)
        return false;

    if (sizeChanges == changesBefore)
        onSize(&requested);

    return true;
}

bool PlugView::unhandledKey(const eacp::Graphics::KeyEvent& key)
{
    return embedding.unhandledKey(key);
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

    embedding.setHostScale(factor);
    return Steinberg::kResultOk;
}

} // namespace MakeASound::VST3
