#pragma once

// VST3-only, after VST3Common.h.

#include "../HostedEditor.h"

#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/gui/iplugview.h>

namespace MakeASound::Hosting
{

// A VST3 plugin's IPlugView in an eacp view. Its ViewRects are logical points on
// macOS and physical pixels on Windows, converted against the DPI of the child
// window the view is parented into, which a plugin also learns through
// IPlugViewContentScaleSupport::setContentScaleFactor: at 200% a plugin reports
// twice its size once told, and taking either figure for points would open a
// window twice too big.
class HostedVST3Editor : public HostedEditor
{
public:
    // Null when the view cannot be parented on this platform.
    static OwningPointer<Editor> create(Steinberg::IPlugView* view);

    explicit HostedVST3Editor(Steinberg::IPlugView& viewToUse);
    ~HostedVST3Editor() override;

    eacp::Graphics::View& view() override;
    EditorSize initialSize() const override;
    EditorConstraints constraints() const override;
    EditorSize constrainSize(EditorSize proposed) const override;

    void onAttached() override;
    void onRemoved() override;

private:
    struct Impl;
    OwningPointer<Impl> impl;
};

// What differs per platform (VST3EditorPlatform-<os>.cpp).
namespace VST3EditorPlatform
{
// kPlatformTypeNSView or kPlatformTypeHWND.
Steinberg::FIDString viewType() noexcept;

// Physical pixels per point for the native window, 1 on macOS.
float scaleFor(void* nativeParent) noexcept;

// What the plugin is told of the scale: nothing on macOS but a 1 before
// attaching (some plugins otherwise double their own size), the scale on
// Windows.
void announceBeforeAttach(Steinberg::IPlugView& view) noexcept;
void announceScale(Steinberg::IPlugView& view, float scale) noexcept;
} // namespace VST3EditorPlatform

} // namespace MakeASound::Hosting
