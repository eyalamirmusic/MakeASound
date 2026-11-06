#pragma once

// macOS only: an Audio Unit's own view in an eacp view.

#include "../HostedEditor.h"

#include <AudioToolbox/AudioToolbox.h>

namespace MakeASound::Hosting
{

// The unit's custom Cocoa view (kAudioUnitProperty_CocoaUI, which an AUv3 behind
// the v2 API answers too), or Apple's generic parameter view for a unit with
// none. A view that changes its own frame asks the host to resize to it.
class HostedAudioUnitEditor : public HostedEditor
{
public:
    explicit HostedAudioUnitEditor(AudioUnit unitToUse);
    ~HostedAudioUnitEditor() override;

    eacp::Graphics::View& view() override;
    EditorSize initialSize() const override;

    // Resizable only for the generic view: a custom view sizes itself.
    EditorConstraints constraints() const override;
    EditorSize constrainSize(EditorSize proposed) const override;

    void onAttached() override;
    void onRemoved() override;

    // Whether the unit's own view was found, rather than the generic one.
    bool hasCustomView() const;

private:
    struct Impl;
    OwningPointer<Impl> impl;
};

} // namespace MakeASound::Hosting
