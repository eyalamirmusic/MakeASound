#pragma once

#include "VST3Common.h"
#include "HostRunLoop.h"
#include "../Core/Editor.h"
#include "../../Common/Common.h"

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/common/pluginview.h"

namespace eacp::Graphics
{
class EmbeddedView;
}

namespace MakeASound::VST3
{

// The host's window onto a plugin's Editor, embedded through eacp. The editor
// lives as long as the view, the embedding only while the host has it attached.
// The platform's half is in PlugView-<platform>.cpp: the view type the host hands
// attached(), the currency of a ViewRect and how the content scale reaches the
// embedded view.
class PlugView
    : public Steinberg::CPluginView
    , public Steinberg::IPlugViewContentScaleSupport
{
public:
    PlugView(Steinberg::IPtr<FUnknown> ownerToKeep,
             OwningPointer<Editor> editorToShow);
    ~PlugView() override;

    static FIDString nativeViewType() noexcept;

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override;
    tresult PLUGIN_API attached(void* parent, FIDString type) override;
    tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override;
    tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    tresult PLUGIN_API canResize() override;
    tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;
    tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* frame) override;
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override;

    OBJ_METHODS(PlugView, Steinberg::CPluginView)
    DEFINE_INTERFACES
    DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(Steinberg::CPluginView)
    REFCOUNT_METHODS(Steinberg::CPluginView)

private:
    void attachedToParent() override;
    void removedFromParent() override;

    // Pixels per point in a ViewRect: 1 where the host speaks points.
    float rectScale() const noexcept;
    // Hands `scale` to the embedded view where the platform leaves that to us.
    void applyContentScale() noexcept;
    Steinberg::ViewRect rectFor(EditorSize size) const noexcept;

    Steinberg::IPtr<FUnknown> owner;
    OwningPointer<Editor> editor;
    OwningPointer<eacp::Graphics::EmbeddedView> embedded;
    OwningPointer<HostRunLoop> frameLoop;
    float scale = 1.f;
    bool sizedByHost = false;
};

} // namespace MakeASound::VST3
