#include "../../Plugin/VST3/VST3Common.h"
#include "HostedVST3Editor.h"
#include "../UI/NativeViewContainer.h"

#include <base/source/fobject.h>

#include <cmath>
#include <optional>

namespace MakeASound::Hosting
{

using namespace Steinberg;

namespace
{
// What a plugin that reports no size opens at.
constexpr auto fallbackSize = EditorSize {800, 600};

int toPixels(int points, float scale)
{
    return static_cast<int>(std::lround(static_cast<float>(points) * scale));
}

int toPoints(int pixels, float scale)
{
    return static_cast<int>(std::lround(static_cast<float>(pixels) / scale));
}

ViewRect rectFor(EditorSize size, float scale)
{
    return {0, 0, toPixels(size.width, scale), toPixels(size.height, scale)};
}

std::optional<EditorSize> reportedSize(IPlugView& view, float scale)
{
    auto rect = ViewRect();

    if (view.getSize(&rect) != kResultOk || rect.getWidth() <= 0
        || rect.getHeight() <= 0)
        return std::nullopt;

    return EditorSize {toPoints(rect.getWidth(), scale),
                       toPoints(rect.getHeight(), scale)};
}

EditorSize sizeOf(const eacp::Graphics::View& view)
{
    auto bounds = view.getLocalBounds();
    return {static_cast<int>(bounds.w), static_cast<int>(bounds.h)};
}
} // namespace

struct HostedVST3Editor::Impl
{
    // The IPlugFrame the plugin asks to be resized through.
    struct Frame
        : public FObject
        , public IPlugFrame
    {
        explicit Frame(Impl& ownerToUse)
            : owner(&ownerToUse)
        {
        }

        tresult PLUGIN_API resizeView(IPlugView*, ViewRect* newSize) override
        {
            if (newSize == nullptr || owner == nullptr)
                return kResultFalse;

            auto size = EditorSize {toPoints(newSize->getWidth(), owner->scale),
                                    toPoints(newSize->getHeight(), owner->scale)};

            return owner->editor.requestResize(size) ? kResultOk : kResultFalse;
        }

        void detach() noexcept { owner = nullptr; }

        // clang-format off
        OBJ_METHODS(Frame, FObject)
        REFCOUNT_METHODS(FObject)
        DEFINE_INTERFACES
            DEF_INTERFACE(IPlugFrame)
        END_DEFINE_INTERFACES(FObject)
        // clang-format on

        Impl* owner = nullptr;
    };

    Impl(HostedVST3Editor& editorToUse, IPlugView& viewToUse)
        : editor(editorToUse)
        , plugView(&viewToUse)
    {
    }

    ~Impl()
    {
        if (frame)
            frame->detach();

        if (container)
            container->onResized = {};
    }

    void setScale(float scaleToUse)
    {
        if (scaleToUse <= 0.f || scaleToUse == scale)
            return;

        scale = scaleToUse;
        VST3EditorPlatform::announceScale(*plugView, scale);
    }

    // The container moved: the plugin follows, at the window's current scale.
    void containerResized()
    {
        if (!attached)
            return;

        setScale(VST3EditorPlatform::scaleFor(nativeParent));

        auto rect = rectFor(sizeOf(*container), scale);
        plugView->onSize(&rect);
    }

    HostedVST3Editor& editor;
    IPtr<IPlugView> plugView;
    OwningPointer<NativeViewContainer> container;
    IPtr<Frame> frame;
    void* nativeParent = nullptr;
    float scale = 0.f;
    bool attached = false;
};

OwningPointer<Editor> HostedVST3Editor::create(IPlugView* view)
{
    auto owned = IPtr<IPlugView>(view, false);
    auto type = VST3EditorPlatform::viewType();

    if (!owned || owned->isPlatformTypeSupported(type) != kResultTrue)
        return {};

    return EA::makeOwned<HostedVST3Editor>(*owned);
}

HostedVST3Editor::HostedVST3Editor(IPlugView& viewToUse)
    : impl(EA::makeOwned<Impl>(*this, viewToUse))
{
    impl->setScale(VST3EditorPlatform::scaleFor(nullptr));
}

HostedVST3Editor::~HostedVST3Editor()
{
    onRemoved();
}

EditorSize HostedVST3Editor::initialSize() const
{
    return reportedSize(*impl->plugView, impl->scale).value_or(fallbackSize);
}

EditorConstraints HostedVST3Editor::constraints() const
{
    auto result = EditorConstraints();
    result.resizable = impl->plugView->canResize() == kResultTrue;
    return result;
}

EditorSize HostedVST3Editor::constrainSize(EditorSize proposed) const
{
    auto scale = impl->scale;
    auto rect = rectFor(proposed, scale);

    if (impl->plugView->checkSizeConstraint(&rect) != kResultOk
        || rect.getWidth() <= 0 || rect.getHeight() <= 0)
        return proposed;

    return {toPoints(rect.getWidth(), scale), toPoints(rect.getHeight(), scale)};
}

eacp::Graphics::View& HostedVST3Editor::view()
{
    if (!impl->container)
    {
        impl->container = EA::makeOwned<NativeViewContainer>();
        impl->container->onResized = [owner = impl.get()](int, int)
        { owner->containerResized(); };

        auto size = initialSize();
        impl->container->setBounds({0.f,
                                    0.f,
                                    static_cast<float>(size.width),
                                    static_cast<float>(size.height)});
    }

    return *impl->container;
}

void HostedVST3Editor::onAttached()
{
    if (impl->attached)
        return;

    auto& container = static_cast<NativeViewContainer&>(view());

    // The earliest a Windows child window exists: it needs its parent first.
    auto* parent = container.getNativeParentHandle();

    if (parent == nullptr)
        return;

    impl->nativeParent = parent;
    impl->setScale(VST3EditorPlatform::scaleFor(parent));

    impl->frame = owned(new Impl::Frame(*impl));
    impl->plugView->setFrame(impl->frame);
    VST3EditorPlatform::announceBeforeAttach(*impl->plugView);

    if (impl->plugView->attached(parent, VST3EditorPlatform::viewType())
        != kResultOk)
    {
        impl->plugView->setFrame(nullptr);
        impl->frame->detach();
        impl->frame = nullptr;
        impl->nativeParent = nullptr;
        return;
    }

    impl->attached = true;

    // A plugin that learned its scale only now may report another size.
    if (auto reported = reportedSize(*impl->plugView, impl->scale))
    {
        auto current = sizeOf(container);

        if (reported->width != current.width || reported->height != current.height)
            requestResize(*reported);
    }

    auto rect = rectFor(sizeOf(container), impl->scale);
    impl->plugView->onSize(&rect);
}

void HostedVST3Editor::onRemoved()
{
    if (!impl->attached)
        return;

    impl->attached = false;
    impl->nativeParent = nullptr;
    impl->plugView->removed();
    impl->plugView->setFrame(nullptr);

    if (impl->frame)
        impl->frame->detach();

    impl->frame = nullptr;
}

} // namespace MakeASound::Hosting
