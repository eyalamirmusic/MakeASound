#pragma once

#include "Gui.h"
#include "../Core/Editor.h"
#include "../UI/EmbeddedEditor.h"
#include "../../Common/Common.h"

#include <clap/clap.h>

#include <cstdint>

namespace MakeASound::CLAP
{

// The host's window onto a plugin's Editor, embedded only (never floating),
// through EmbeddedEditor, and the editor's EditorHost: a resize request goes to
// the host's clap_host_gui. The editor lives from create to destroy, the
// embedding from set_parent on. A size is in EmbeddedEditor's host units,
// which is what CLAP's window APIs say: logical points for cocoa, physical
// pixels for win32 and x11. The platform's half is in Gui-<platform>.cpp: the
// window API, the native handle a clap_window carries, and whether the host's
// scale applies. Main thread.
class GuiView final : public EditorHost
{
public:
    GuiView(const clap_host* hostToUse, OwningPointer<Editor> editorToShow);
    ~GuiView() override;

    GuiView(const GuiView&) = delete;
    GuiView& operator=(const GuiView&) = delete;

    // Gui-<platform>.cpp: CLAP_WINDOW_API_COCOA, _WIN32 or _X11, and the
    // window's handle in eacp's currency (an X11 id widened into the pointer).
    static const char* nativeApi() noexcept;
    static void* nativeParentOf(const clap_window& window) noexcept;

    // Gui-<platform>.cpp: false on macOS, where sizes are points and AppKit
    // scales; elsewhere the host's pixels per point (applyHostScale).
    bool setScale(double scale) noexcept;

    bool getSize(uint32_t& width, uint32_t& height);
    bool canResize();
    bool resizeHints(clap_gui_resize_hints& hints);
    bool adjustSize(uint32_t& width, uint32_t& height);
    bool setSize(uint32_t width, uint32_t height);
    bool setParent(const clap_window& window);
    bool setVisible(bool shouldBeVisible);

    bool resizeEditor(EditorSize size) override;
    bool drawsResizeHandle() const override { return true; }
    bool unhandledKey(const eacp::Graphics::KeyEvent& key) override;

private:
    // Takes the host's scale, keeping a size the host set the same in points.
    bool applyHostScale(double scale) noexcept;

    // The size the host now holds, from set_size or an accepted request.
    void applySize(EditorSize units);

    const clap_host* host;
    const clap_host_gui* hostGui = nullptr;
    EmbeddedEditor embedding;
    EditorSize size;
    bool sizedByHost = false;
    bool visible = true;
    int sizeChanges = 0;
};

} // namespace MakeASound::CLAP
