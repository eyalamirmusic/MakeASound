#pragma once

// The CLAP gui suites' shared pieces: a TestEditor standing in for the test
// plugins' createEditor(), a gui created through the plugin's clap.gui as a host
// creates it, and a native window of the platform's standing in for the DAW's
// (CLAPGuiTests-<platform>), into which set_parent embeds the editor.

#include "CLAPTestHost.h"
#include "TestEditor.h"

#include <clap/clap.h>

#include <NanoTest/NanoTest.h>

#include <memory>
#include <utility>

namespace CLAPGuiHost
{
using nano::check;
using namespace MakeASound;
using namespace TestPlugins;
using namespace CLAPHost;

// Points the test plugins' createEditor() at a TestEditor for its lifetime.
struct ScopedTestEditor
{
    explicit ScopedTestEditor(EditorConstraints limits = {},
                              EditorSize size = {400, 300})
    {
        Module::editorFactory = [this, limits, size](Plugin&)
        {
            auto created = EA::makeOwned<TestEditor>();
            created->limits = limits;
            created->size = size;
            editor = created.get();
            return OwningPointer<Editor>(std::move(created));
        };
    }

    ~ScopedTestEditor() { Module::editorFactory = nullptr; }

    ScopedTestEditor(const ScopedTestEditor&) = delete;
    ScopedTestEditor& operator=(const ScopedTestEditor&) = delete;

    TestEditor* editor = nullptr;
};

inline EditorConstraints bounded()
{
    return {.resizable = true, .minimum = {200, 100}, .maximum = {800, 600}};
}

inline EditorConstraints ratioLocked()
{
    return {.resizable = true,
            .minimum = {150, 100},
            .maximum = {1500, 1000},
            .aspectRatio = {3, 2}};
}

struct Size
{
    uint32_t width = 0;
    uint32_t height = 0;

    bool is(int w, int h) const
    {
        return static_cast<int>(width) == w && static_cast<int>(height) == h;
    }
};

// The plugin's gui created in the platform's preferred API, embedded, and
// destroyed with the fixture, as a host does it.
struct Gui
{
    template <typename P>
    explicit Gui(PluginHost<P>& pluginHost)
        : clap(pluginHost.clap)
        , gui(pluginHost.gui)
    {
        if (gui == nullptr)
            return;

        auto isFloating = true;
        gui->get_preferred_api(clap, &api, &isFloating);
        created = gui->create(clap, api, false);
    }

    ~Gui()
    {
        if (created)
            gui->destroy(clap);
    }

    Gui(const Gui&) = delete;
    Gui& operator=(const Gui&) = delete;

    Size size() const
    {
        auto result = Size {};
        check(gui->get_size(clap, &result.width, &result.height));
        return result;
    }

    // adjust_size's answer, and whether it gave one.
    std::pair<bool, Size> adjusted(uint32_t width, uint32_t height) const
    {
        auto result = Size {width, height};
        auto adjustedSize = gui->adjust_size(clap, &result.width, &result.height);
        return {adjustedSize, result};
    }

    clap_gui_resize_hints hints() const
    {
        auto result = clap_gui_resize_hints {};
        check(gui->get_resize_hints(clap, &result));
        return result;
    }

    void destroy()
    {
        if (created)
            gui->destroy(clap);

        created = false;
    }

    const clap_plugin* clap;
    const clap_plugin_gui* gui;
    const char* api = nullptr;
    bool created = false;
};

// A window of the platform's in which a host would embed the gui (an NSView, a
// hidden top-level HWND, an X11 window), destroyed on the way out; invalid,
// and the case skips, where none can be made (no X server). What it reads
// back about the plugin's surface is in host units, -1 or {} where unreadable.
class ParentWindow
{
public:
    ParentWindow();
    ~ParentWindow();

    ParentWindow(const ParentWindow&) = delete;
    ParentWindow& operator=(const ParentWindow&) = delete;

    bool isValid() const;
    clap_window window() const;

    int childCount() const;
    EditorSize childSize() const;
    bool childVisible() const;

    // Pumps the message loop until `ready`, for up to three seconds.
    template <typename Predicate>
    static bool waitFor(Predicate ready)
    {
        for (auto attempt = 0; attempt < 150 && !ready(); ++attempt)
            pump();

        return ready();
    }

private:
    static void pump();

    struct Native;
    std::unique_ptr<Native> native;
};

} // namespace CLAPGuiHost
