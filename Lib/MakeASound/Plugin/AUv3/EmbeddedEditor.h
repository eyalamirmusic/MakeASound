#pragma once

// Objective-C++ only: the half of the AUv3 view both platforms share, so each
// view controller (AUv3View-iOS.mm over UIKit, AUv3View-macOS.mm over AppKit)
// only translates its framework's calls into these.

#include "../Core/Editor.h"
#include "../../Common/Common.h"

#import <CoreAudioKit/AUViewController.h>

#include <functional>

namespace eacp::Graphics
{
class EmbeddedView;
}

namespace MakeASound
{
class Plugin;
}

namespace MakeASound::AUv3
{

class Adapter;

// The plugin's editor, or GenericEditor, embedded through eacp over a view
// controller's view, and the editor's EditorHost: a resize request becomes the
// controller's preferredContentSize, which is how an AUv3 asks its host for a
// size. Message thread.
class EmbeddedEditor final : public EditorHost
{
public:
    // Answers a resize request: true when the controller took the size.
    using SizeRequest = std::function<bool(EditorSize)>;

    // Builds the editor; nothing is shown until attach().
    explicit EmbeddedEditor(Adapter& adapter);
    ~EmbeddedEditor() override;

    EmbeddedEditor(const EmbeddedEditor&) = delete;
    EmbeddedEditor& operator=(const EmbeddedEditor&) = delete;

    // constrain(initialSize()): the preferredContentSize the controller opens at.
    EditorSize openingSize() const;

    // Embeds the editor over `nativeView` (a UIView* or an NSView*) at `size`, the
    // view's bounds, then setHost and onAttached(), and registers with the
    // adapter's closers, since the host may keep the view past the unit.
    void attach(void* nativeView, EditorSize size, SizeRequest request);

    // The host gave the view a new size: the editor takes it unconstrained, as
    // every format takes a host's resize.
    void hostResized(EditorSize size);

    // onRemoved() and setHost(nullptr), then the embedding and the editor go.
    // Idempotent; the adapter runs it before it goes.
    void close();

    bool isOpen() const noexcept { return editor != nullptr; }

    bool resizeEditor(EditorSize size) override;

    // No AUv3 host draws a grip over a plugin's view, so the editor's ResizeGrip
    // rule decides.
    bool drawsResizeHandle() const override { return false; }

    // Whether an editor of this policy fits a host's view configuration: the
    // smallest size it can take, constrain(minimum) for a resizable editor and
    // its opening size for a fixed one, is no larger than the configuration on
    // either side, so nothing of it is cut off; a zero side matches any, as
    // AUAudioUnitViewConfiguration defines it. A configuration larger than the
    // maximum still fits: a host's size reaches the editor unconstrained.
    static bool fits(const EditorConstraints& limits,
                     EditorSize initial,
                     EditorSize configuration) noexcept;

    // supportedViewConfigurations: for a unit of `plugin`, over a throwaway editor
    // (createEditor() is cheap by contract). Off the main thread it asks there.
    static NSIndexSet*
        supportedConfigurations(Plugin& plugin,
                                NSArray<AUAudioUnitViewConfiguration*>* available);

private:
    Adapter* owner = nullptr;
    OwningPointer<Editor> editor;
    OwningPointer<eacp::Graphics::EmbeddedView> embedded;
    SizeRequest sizeRequest;
};

} // namespace MakeASound::AUv3
