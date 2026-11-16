#pragma once

#include "VST3Common.h"
#include "HostRunLoop.h"
#include "../Core/Editor.h"
#include "../UI/EmbeddedEditor.h"
#include "../../Common/Common.h"

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/common/pluginview.h"

namespace MakeASound::VST3
{

// The host's window onto a plugin's Editor, embedded through EmbeddedEditor, and
// the editor's EditorHost: a resize request goes to the host's IPlugFrame. The
// editor lives as long as the view, the embedding only while the host has it
// attached. A ViewRect is in EmbeddedEditor's host units: points on macOS,
// physical pixels on Windows and X11. The platform's half is the view type the
// host hands attached(), in PlugView-<platform>.cpp; the rest of it, the scale
// and the keys, is EmbeddedEditor's.
class PlugView
    : public Steinberg::CPluginView
    , public Steinberg::IPlugViewContentScaleSupport
    , public EditorHost
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

    bool resizeEditor(EditorSize size) override;
    bool drawsResizeHandle() const override { return true; }

    // On Windows, posted to the host's window while attached; elsewhere left to
    // the view (EmbeddedEditor::unhandledKey).
    bool unhandledKey(const eacp::Graphics::KeyEvent& key) override;

    OBJ_METHODS(PlugView, Steinberg::CPluginView)
    DEFINE_INTERFACES
    DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(Steinberg::CPluginView)
    REFCOUNT_METHODS(Steinberg::CPluginView)

private:
    void attachedToParent() override;
    void removedFromParent() override;

    Steinberg::ViewRect rectFor(EditorSize units) const noexcept;
    Steinberg::ViewRect openingRect() const;

    Steinberg::IPtr<FUnknown> owner;
    EmbeddedEditor embedding;
    OwningPointer<HostRunLoop> frameLoop;
    bool sizedByHost = false;
    int sizeChanges = 0;
};

} // namespace MakeASound::VST3
