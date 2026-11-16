#pragma once

// A CLAP plugin written against the C API alone whose only extension is
// clap.gui, for the hosted CLAP editor suites: it records every call the host
// makes into its GUI, flags each that breaks the spec (off the main thread,
// without a GUI, twice created or parented, shown before it is parented, scaled
// under cocoa, anything floating, anything after the plugin was destroyed), and
// calls the host's clap_host_gui on request, from whichever thread the test
// runs it on. MakeASound's own adapter (the fixtures) behaves, so this is what
// keeps it from masking a host that does not.

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/CLAP/CLAPHostedPlugin.h>

#include <eacp/Core/Threads/ThreadUtils.h>

#include <NanoTest/NanoTest.h>

#include <clap/clap.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace CLAPGuiStub
{
using namespace MakeASound;
using namespace MakeASound::Hosting;

struct Size
{
    uint32_t width = 0;
    uint32_t height = 0;

    bool is(uint32_t w, uint32_t h) const { return width == w && height == h; }
};

struct Plugin
{
    Plugin()
    {
        descriptor.clap_version = CLAP_VERSION;
        descriptor.id = "com.makeasound.tests.gui-stub";
        descriptor.name = "Gui Stub";
        descriptor.vendor = "MakeASound Tests";
        descriptor.url = "";
        descriptor.manual_url = "";
        descriptor.support_url = "";
        descriptor.version = "1.0";
        descriptor.description = "";
        descriptor.features = features;

        clap.desc = &descriptor;
        clap.plugin_data = this;
        clap.init = [](const clap_plugin* p) { return of(p).init(); };
        clap.destroy = [](const clap_plugin* p) { of(p).destroyPlugin(); };
        clap.activate = [](const clap_plugin*, double, uint32_t, uint32_t)
        { return true; };
        clap.deactivate = [](const clap_plugin*) {};
        clap.start_processing = [](const clap_plugin*) { return true; };
        clap.stop_processing = [](const clap_plugin*) {};
        clap.reset = [](const clap_plugin*) {};
        clap.process = [](const clap_plugin*, const clap_process*)
        { return clap_process_status {CLAP_PROCESS_CONTINUE}; };
        clap.get_extension = [](const clap_plugin* p, const char* id)
        {
            return std::strcmp(id, CLAP_EXT_GUI) == 0
                       ? static_cast<const void*>(&of(p).gui)
                       : nullptr;
        };
        clap.on_main_thread = [](const clap_plugin*) {};

        gui.is_api_supported =
            [](const clap_plugin* p, const char* api, bool isFloating)
        {
            of(p).record("is_api_supported");
            return !isFloating && isNative(api);
        };
        gui.get_preferred_api = [](const clap_plugin* p, const char**, bool*)
        {
            of(p).record("get_preferred_api");
            return false;
        };
        gui.create = [](const clap_plugin* p, const char* api, bool isFloating)
        { return of(p).create(api, isFloating); };
        gui.destroy = [](const clap_plugin* p)
        {
            auto& self = of(p);
            self.needsGui("destroy");

            if (self.inOwnCall)
                self.violation("destroy inside the plugin's own call");

            self.created = false;
            self.parented = false;
            self.visible = false;
            self.lost = false;
        };
        gui.set_scale = [](const clap_plugin* p, double scale)
        {
            auto& self = of(p);
            self.needsGui("set_scale");

            if (self.api == CLAP_WINDOW_API_COCOA)
                self.violation("set_scale under cocoa");

            self.scale = scale;
            self.runInside(self.duringSetScale);
            return true;
        };
        gui.get_size = [](const clap_plugin* p, uint32_t* width, uint32_t* height)
        {
            auto& self = of(p);
            self.needsGui("get_size");
            *width = self.size.width;
            *height = self.size.height;
            return true;
        };
        gui.can_resize = [](const clap_plugin* p)
        {
            auto& self = of(p);
            self.needsGui("can_resize");
            return self.resizable;
        };
        gui.get_resize_hints = [](const clap_plugin* p, clap_gui_resize_hints* hints)
        {
            auto& self = of(p);
            self.needsGui("get_resize_hints");

            if (!self.hasHints)
                return false;

            *hints = self.hints;
            return true;
        };
        gui.adjust_size = [](const clap_plugin* p, uint32_t* width, uint32_t* height)
        {
            auto& self = of(p);
            self.needsGui("adjust_size");

            if (!self.resizable)
                return false;

            *width = self.adjust(*width);
            *height = self.adjust(*height);
            return true;
        };
        gui.set_size = [](const clap_plugin* p, uint32_t width, uint32_t height)
        {
            auto& self = of(p);
            self.needsGui("set_size");

            if (!self.resizable)
                self.violation("set_size on a gui that cannot resize");

            self.size = {width, height};
            self.sizesSet.push_back({width, height});
            return true;
        };
        gui.set_parent = [](const clap_plugin* p, const clap_window* window)
        {
            auto& self = of(p);
            self.needsGui("set_parent");

            if (self.parented)
                self.violation("set_parent twice");

            if (window == nullptr || window->api == nullptr
                || std::strcmp(window->api, self.api.c_str()) != 0)
                self.violation("set_parent in another api");

            self.parented = true;
            self.runInside(self.duringSetParent);
            return true;
        };
        gui.set_transient = [](const clap_plugin* p, const clap_window*)
        {
            of(p).violation("set_transient on an embedded gui");
            return false;
        };
        gui.suggest_title = [](const clap_plugin* p, const char*)
        { of(p).violation("suggest_title on an embedded gui"); };
        gui.show = [](const clap_plugin* p)
        {
            auto& self = of(p);
            self.needsGui("show");

            if (!self.parented)
                self.violation("show before set_parent");

            self.visible = true;
            self.runInside(self.duringShow);
            return true;
        };
        gui.hide = [](const clap_plugin* p)
        {
            auto& self = of(p);
            self.needsGui("hide");
            self.visible = false;
            return true;
        };
    }

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    static Plugin& of(const clap_plugin* plugin)
    {
        return *static_cast<Plugin*>(plugin->plugin_data);
    }

    static bool isNative(const char* api)
    {
        return api != nullptr
               && (std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0
                   || std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0
                   || std::strcmp(api, CLAP_WINDOW_API_X11) == 0);
    }

    const clap_plugin* make(const clap_host* hostToUse)
    {
        host = hostToUse;
        return &clap;
    }

    bool init()
    {
        hostGui = static_cast<const clap_host_gui*>(
            host->get_extension(host, CLAP_EXT_GUI));
        return true;
    }

    void destroyPlugin()
    {
        record("plugin destroy");

        if (created)
            violation("plugin destroyed with its gui alive");

        destroyed = true;
    }

    bool create(const char* apiToUse, bool isFloating)
    {
        record("create");

        if (created)
            violation("create twice");

        if (isFloating)
            violation("create floating");

        if (!createSucceeds)
            return false;

        api = apiToUse != nullptr ? apiToUse : "";
        created = true;
        return true;
    }

    // Rounds down to a multiple of `step`, never under minimum.
    uint32_t adjust(uint32_t value) const
    {
        return std::max(minimum, value - value % step);
    }

    void violation(std::string what) { violations.push_back(std::move(what)); }

    void record(const char* name)
    {
        calls.emplace_back(name);

        if (!eacp::Threads::isMainThread())
            violation(std::string(name) + " off the main thread");

        if (destroyed)
            violation(std::string(name) + " after the plugin was destroyed");
    }

    void needsGui(const char* name)
    {
        record(name);

        if (!created)
            violation(std::string(name) + " without a gui");

        if (lost && std::strcmp(name, "destroy") != 0)
            violation(std::string(name) + " after closed(true)");
    }

    // Runs `what` (when set) as the plugin, inside the call it is in.
    void runInside(const std::function<void()>& what)
    {
        if (!what)
            return;

        auto was = inOwnCall;
        inOwnCall = true;
        what();
        inOwnCall = was;
    }

    int count(const char* name) const
    {
        return static_cast<int>(std::count(calls.begin(), calls.end(), name));
    }

    // The calls since `mark`, dropping the queries a host may make any time.
    std::vector<std::string> callsSince(size_t mark) const
    {
        auto result = std::vector<std::string>();

        for (auto i = mark; i < calls.size(); ++i)
            if (calls[i] != "can_resize" && calls[i] != "get_resize_hints"
                && calls[i] != "adjust_size")
                result.push_back(calls[i]);

        return result;
    }

    // callsSince() without get_size either: what creates, parents, shows, hides
    // and destroys the GUI.
    std::vector<std::string> lifecycleSince(size_t mark) const
    {
        auto result = callsSince(mark);
        result.erase(std::remove(result.begin(), result.end(), "get_size"),
                     result.end());
        return result;
    }

    // Whether, queries aside, the call before the first `name` since `mark` was
    // `before`.
    bool precededBy(size_t mark, const char* name, const char* before) const
    {
        auto since = callsSince(mark);

        for (auto i = size_t {1}; i < since.size(); ++i)
            if (since[i] == name)
                return since[i - 1] == before;

        return false;
    }

    // The host's clap_host_gui, as the plugin calls it. A resize the host takes
    // is the GUI's size from then on, as the spec has it.
    bool requestResize(uint32_t width, uint32_t height)
    {
        auto accepted = hostGui->request_resize(host, width, height);

        if (accepted && eacp::Threads::isMainThread())
            size = {width, height};

        return accepted;
    }

    bool requestShow() { return hostGui->request_show(host); }
    bool requestHide() { return hostGui->request_hide(host); }
    void hintsChanged() { hostGui->resize_hints_changed(host); }
    // With `wasDestroyed`, the GUI takes no call but destroy() from now on.
    void closed(bool wasDestroyed)
    {
        if (wasDestroyed && created)
            lost = true;

        hostGui->closed(host, wasDestroyed);
    }

    // Runs `call` on a thread of its own, as a plugin's worker would.
    template <typename Call>
    auto fromAnotherThread(Call call)
    {
        auto result = decltype(call()) {};
        auto worker = std::thread([&] { result = call(); });
        worker.join();
        return result;
    }

    const char* features[1] = {nullptr};
    clap_plugin_descriptor descriptor {};
    clap_plugin clap {};
    clap_plugin_gui gui {};

    const clap_host* host = nullptr;
    const clap_host_gui* hostGui = nullptr;

    // What the GUI does.
    Size size {300, 200};
    bool resizable = true;
    bool hasHints = true;
    clap_gui_resize_hints hints {true, true, false, 0, 0};
    uint32_t step = 10;
    uint32_t minimum = 100;
    bool createSucceeds = true;

    // What the plugin does from inside set_scale, set_parent and show.
    std::function<void()> duringSetScale;
    std::function<void()> duringSetParent;
    std::function<void()> duringShow;

    // What the host did.
    std::vector<std::string> calls;
    std::vector<std::string> violations;
    std::vector<Size> sizesSet;
    std::string api;
    double scale = 0.0;
    bool created = false;
    bool parented = false;
    bool visible = false;
    bool destroyed = false;

    // Closed with was_destroyed, and not destroyed by the host yet; inside one
    // of its own calls (running a during* hook).
    bool lost = false;
    bool inOwnCall = false;
};

// The stub loaded as a host loads a CLAP plugin, with `editors` as its editor
// factory.
inline OwningPointer<CLAPHostedPlugin> load(Plugin& stub,
                                            HostedEditorFactory editors)
{
    auto description = PluginDescription();
    description.name = "Gui Stub";
    description.classId = stub.descriptor.id;

    auto result = CLAPHostedPlugin::create(
        description,
        nullptr,
        [&stub](const clap_host* host) { return stub.make(host); },
        {.name = "Gui Stub Host", .editors = std::move(editors)});

    nano::check(result.error.empty());
    nano::check(result.plugin != nullptr);

    return OwningPointer<CLAPHostedPlugin>(
        static_cast<CLAPHostedPlugin*>(result.plugin.release()));
}

inline bool isClean(const Plugin& stub)
{
    for (const auto& violation: stub.violations)
        std::fprintf(stderr, "clap.gui spec violation: %s\n", violation.c_str());

    return stub.violations.empty();
}

} // namespace CLAPGuiStub
