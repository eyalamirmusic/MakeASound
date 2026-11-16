#include "HostedCLAPEditor.h"
#include "../UI/NativeViewContainer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace MakeASound::Hosting
{

namespace
{
// Named apart from HostedVST3Editor.cpp's helpers, which share a unity batch
// with these.
namespace CLAPEditorSizes
{
// What a plugin that reports no size opens at.
constexpr auto fallbackGuiSize = EditorSize {800, 600};

// HostedCLAPEditor::overrideScaleForTesting(): 0 for the platform's.
auto scaleForTesting = 0.f;

int toInt(uint32_t value) noexcept
{
    return static_cast<int>(
        std::min(value, static_cast<uint32_t>(std::numeric_limits<int>::max())));
}

uint32_t toUnits(int points, float scale) noexcept
{
    return static_cast<uint32_t>(
        std::max(0L, std::lround(static_cast<float>(points) * scale)));
}

int unitsToPoints(uint32_t units, float scale) noexcept
{
    return static_cast<int>(std::lround(static_cast<float>(toInt(units)) / scale));
}

EditorSize boundsOf(const eacp::Graphics::View& view)
{
    auto bounds = view.getLocalBounds();
    return {static_cast<int>(bounds.w), static_cast<int>(bounds.h)};
}

bool sameSize(EditorSize a, EditorSize b) noexcept
{
    return a.width == b.width && a.height == b.height;
}

// Counts a call into the GUI for as long as it runs.
struct CLAPGuiCallScope
{
    explicit CLAPGuiCallScope(int& depthToUse) noexcept
        : depth(depthToUse)
    {
        ++depth;
    }

    ~CLAPGuiCallScope() { --depth; }

    CLAPGuiCallScope(const CLAPGuiCallScope&) = delete;
    CLAPGuiCallScope& operator=(const CLAPGuiCallScope&) = delete;

    int& depth;
};
} // namespace CLAPEditorSizes
} // namespace

using namespace CLAPEditorSizes;

// The editor's state, and the listener the plugin's clap_host_gui calls reach.
//
// The GUI is `created` from create() until destroy(), and the editor is the
// plugin's listener for exactly that long. A GUI the plugin says it destroyed
// (closed with was_destroyed) is `lost`: it takes no call but its destroy(),
// which never runs inside one of the plugin's own calls; it waits for the
// deferred closed the next service delivers.
struct HostedCLAPEditor::Impl final : CLAPGuiListener
{
    // A size in the window API's units, as the plugin speaks it. Here, not in
    // the anonymous namespace: a member's type, which gcc would otherwise warn
    // of (-Wsubobject-linkage).
    struct UnitSize
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    Impl(HostedCLAPEditor& editorToUse, CLAPHostedPlugin& pluginToUse)
        : editor(editorToUse)
        , plugin(&pluginToUse)
        , clap(pluginToUse.clapPlugin())
        , gui(pluginToUse.guiExtension())
    {
    }

    ~Impl() override
    {
        destroyGui();
        stopListening();

        if (container)
        {
            container->onResized = {};
            container->onBackingScaleChanged = [](float) {};
        }
    }

    // Everything create() needs to embed a GUI and take it down again.
    static bool isComplete(const clap_plugin_gui* gui) noexcept
    {
        return gui != nullptr && gui->is_api_supported != nullptr
               && gui->create != nullptr && gui->destroy != nullptr
               && gui->get_size != nullptr && gui->set_parent != nullptr
               && gui->show != nullptr && gui->hide != nullptr;
    }

    static float platformScale(eacp::Graphics::NativeChildSurface* surface,
                               void* nativeParent) noexcept
    {
        return scaleForTesting > 0.f
                   ? scaleForTesting
                   : CLAPEditorPlatform::scaleFor(surface, nativeParent);
    }

    // Created, and not lost: a GUI calls may reach.
    bool alive() const noexcept { return created && !lost; }

    // One call into the GUI, counted while it runs (the service delivers
    // nothing from a loop the plugin pumps inside it).
    template <typename Call>
    bool callGui(Call&& call)
    {
        auto scope = CLAPGuiCallScope(callDepth);
        return call();
    }

    bool createGui()
    {
        // A lost GUI waits for its destroy() before there can be another.
        if (created)
            return !lost;

        // One GUI per instance: another editor's is there.
        if (plugin == nullptr || clap == nullptr || !isComplete(gui)
            || plugin->hasGuiListener())
            return false;

        if (!callGui(
                [this]
                {
                    return gui->create(clap, CLAPEditorPlatform::windowApi(), false);
                }))
            return false;

        created = true;
        lost = false;

        // Before set_scale: what the plugin asks from inside it reaches the
        // editor.
        plugin->setGuiListener(this);
        listening = true;

        announceScale();
        return true;
    }

    void stopListening()
    {
        if (listening && plugin != nullptr)
            plugin->setGuiListener(nullptr);

        listening = false;
    }

    // hide (unless lost or never shown), then destroy; the flags drop first, so
    // nothing the plugin calls back from inside reaches the GUI again. The
    // listener goes last, dropping whatever the plugin asked of this GUI.
    void destroyGui()
    {
        if (!created)
            return;

        auto wasShown = shown && !lost;
        created = false;
        lost = false;
        attached = false;
        shown = false;
        pluginUnits.reset();

        {
            auto scope = CLAPGuiCallScope(callDepth);

            if (wasShown)
                gui->hide(clap);

            gui->destroy(clap);
        }

        stopListening();
    }

    void announceScale()
    {
        if (alive() && CLAPEditorPlatform::usesScale() && gui->set_scale != nullptr)
            callGui([this]
                    { return gui->set_scale(clap, static_cast<double>(scale)); });
    }

    void setScale(float scaleToUse)
    {
        if (scaleToUse <= 0.f || scaleToUse == scale)
            return;

        scale = scaleToUse;
        announceScale();
    }

    EditorSize pointsOf(UnitSize units) const noexcept
    {
        return {unitsToPoints(units.width, scale),
                unitsToPoints(units.height, scale)};
    }

    std::optional<EditorSize> reportedSize()
    {
        auto width = uint32_t {0};
        auto height = uint32_t {0};

        if (!alive() || !gui->get_size(clap, &width, &height) || width == 0
            || height == 0)
            return std::nullopt;

        pluginUnits = UnitSize {width, height};
        lastSize = pointsOf(*pluginUnits);
        return lastSize;
    }

    bool readHints(clap_gui_resize_hints& hints) const
    {
        hints = clap_gui_resize_hints {};
        return alive() && gui->get_resize_hints != nullptr
               && gui->get_resize_hints(clap, &hints);
    }

    bool canResize() const
    {
        return alive() && gui->can_resize != nullptr && gui->can_resize(clap);
    }

    EditorSize currentSize() const
    {
        return container ? boundsOf(*container) : lastSize;
    }

    // Whether the plugin is known to have `size` already. Compared in points,
    // the host's units: a size in pixels does not survive the trip to points
    // and back at a fractional scale (601 px at 1.5 is 401 pt, which is 602 px).
    bool pluginHas(EditorSize size) const noexcept
    {
        return pluginUnits && sameSize(pointsOf(*pluginUnits), size);
    }

    // set_size, as the spec has it for a refused resize the plugin was already
    // told yes to.
    void sendSize(EditorSize size)
    {
        if (!alive() || gui->set_size == nullptr)
            return;

        auto units =
            UnitSize {toUnits(size.width, scale), toUnits(size.height, scale)};
        pluginUnits = units;
        callGui([&] { return gui->set_size(clap, units.width, units.height); });
    }

    // The host settled on `size`: the plugin follows, unless it cannot resize
    // (a fixed-size GUI keeps its own size) or has that size already.
    void followHost(EditorSize size)
    {
        if (!pluginHas(size) && canResize())
            sendSize(size);
    }

    // The container moved or its scale did: the plugin follows, at the window's
    // current scale, unless it has that size already (one it asked for itself,
    // which AppKit lays out only on a later pass).
    void containerResized()
    {
        if (!attached || !alive())
            return;

        setScale(platformScale(container.get(), nativeParent));

        auto size = boundsOf(*container);
        lastSize = size;
        followHost(size);
    }

    bool guiResizeRequested(uint32_t width, uint32_t height, bool deferred) override
    {
        if (!alive())
            return false;

        auto size = pointsOf({width, height});

        // The plugin's own resize, honoured whether or not can_resize lets the
        // user drag: a fixed-size GUI still changes its own size. Taken, it is
        // the plugin's size, with no set_size needed (the container following
        // now or on a later layout pass sends none).
        auto had = pluginUnits;
        pluginUnits = UnitSize {width, height};
        auto accepted = editor.requestOwnResize(size);

        if (accepted || !alive())
            return accepted;

        pluginUnits = had;

        // Already told yes from another thread: the spec has a refusal undone
        // through set_size.
        if (deferred)
            sendSize(currentSize());

        return false;
    }

    void guiResizeHintsChanged() override
    {
        if (alive() && editor.onConstraintsChanged)
            editor.onConstraintsChanged();
    }

    bool guiShowRequested() override
    {
        if (!attached || !alive() || !callGui([this] { return gui->show(clap); })
            || !alive())
            return false;

        shown = true;
        return true;
    }

    bool guiHideRequested() override
    {
        if (!attached || !alive() || !callGui([this] { return gui->hide(clap); }))
            return false;

        shown = false;
        return true;
    }

    // An embedded GUI has no window of its own to close: only a lost one
    // matters. It takes no call from here on, and is destroyed once the plugin
    // is out of its own calls, when the closed comes again, deferred.
    void guiClosed(bool wasDestroyed, bool deferred) override
    {
        if (!created)
            return;

        shown = false;

        if (!wasDestroyed)
            return;

        lost = true;
        attached = false;
        pluginUnits.reset();

        if (deferred && callDepth == 0)
            destroyGui();
    }

    bool guiCallInProgress() const noexcept override { return callDepth > 0; }

    void guiPluginDestroying() override
    {
        destroyGui();
        listening = false;
        plugin = nullptr;
        clap = nullptr;
        gui = nullptr;
    }

    HostedCLAPEditor& editor;
    CLAPHostedPlugin* plugin;
    const clap_plugin* clap;
    const clap_plugin_gui* gui;

    OwningPointer<NativeViewContainer> container;
    void* nativeParent = nullptr;
    float scale = 1.f;
    EditorSize lastSize = fallbackGuiSize;

    // The size the plugin has, in its units, as far as this host knows: what
    // get_size answered, set_size sent, or a request_resize taken.
    std::optional<UnitSize> pluginUnits;

    // The calls into the GUI on the stack.
    int callDepth = 0;

    bool listening = false;
    bool created = false;
    bool lost = false;
    bool attached = false;
    bool shown = false;
};

OwningPointer<Editor> HostedCLAPEditor::create(CLAPHostedPlugin& plugin)
{
    auto* gui = plugin.guiExtension();
    auto* clap = plugin.clapPlugin();

    // One GUI per instance: another editor's is still there.
    if (clap == nullptr || !Impl::isComplete(gui) || plugin.hasGuiListener()
        || !gui->is_api_supported(clap, CLAPEditorPlatform::windowApi(), false))
        return {};

    auto editor = EA::makeOwned<HostedCLAPEditor>(plugin);

    if (!editor->isGuiCreated())
        return {};

    return editor;
}

void HostedCLAPEditor::overrideScaleForTesting(float scale) noexcept
{
    scaleForTesting = std::max(scale, 0.f);
}

HostedCLAPEditor::HostedCLAPEditor(CLAPHostedPlugin& plugin)
    : impl(EA::makeOwned<Impl>(*this, plugin))
{
    impl->scale = std::max(Impl::platformScale(nullptr, nullptr), 0.01f);
    impl->createGui();
    impl->reportedSize();
}

HostedCLAPEditor::~HostedCLAPEditor()
{
    onRemoved();
}

bool HostedCLAPEditor::isGuiCreated() const noexcept
{
    return impl->alive();
}

bool HostedCLAPEditor::isGuiAttached() const noexcept
{
    return impl->attached && impl->alive();
}

EditorSize HostedCLAPEditor::initialSize() const
{
    return impl->reportedSize().value_or(impl->lastSize);
}

EditorConstraints HostedCLAPEditor::constraints() const
{
    auto result = EditorConstraints();
    result.resizable = impl->canResize();

    auto hints = clap_gui_resize_hints {};

    if (!result.resizable || !impl->readHints(hints))
        return result;

    auto both = hints.can_resize_horizontally && hints.can_resize_vertically;
    result.resizable = hints.can_resize_horizontally || hints.can_resize_vertically;

    if (both && hints.preserve_aspect_ratio && hints.aspect_ratio_width > 0
        && hints.aspect_ratio_height > 0)
        result.aspectRatio = {toInt(hints.aspect_ratio_width),
                              toInt(hints.aspect_ratio_height)};

    return result;
}

// A fixed-size GUI accepts only its own size. Otherwise an axis the hints lock
// keeps the current size, and adjust_size has the last word.
EditorSize HostedCLAPEditor::constrainSize(EditorSize proposed) const
{
    auto current = impl->currentSize();

    if (!impl->canResize())
        return impl->alive() ? current : proposed;

    auto target = proposed;
    auto hints = clap_gui_resize_hints {};

    if (impl->readHints(hints))
    {
        if (!hints.can_resize_horizontally)
            target.width = current.width;

        if (!hints.can_resize_vertically)
            target.height = current.height;
    }

    auto* gui = impl->gui;
    auto scale = impl->scale;
    auto width = toUnits(target.width, scale);
    auto height = toUnits(target.height, scale);

    if (gui->adjust_size == nullptr || !gui->adjust_size(impl->clap, &width, &height)
        || width == 0 || height == 0)
        return target;

    return {unitsToPoints(width, scale), unitsToPoints(height, scale)};
}

eacp::Graphics::View& HostedCLAPEditor::view()
{
    if (!impl->container)
    {
        impl->container = EA::makeOwned<NativeViewContainer>();
        impl->container->onResized = [owner = impl.get()](int, int)
        { owner->containerResized(); };
        CLAPEditorPlatform::watchScale(
            *impl->container, [owner = impl.get()] { owner->containerResized(); });

        auto size = initialSize();
        impl->container->setBounds({0.f,
                                    0.f,
                                    static_cast<float>(size.width),
                                    static_cast<float>(size.height)});
    }

    return *impl->container;
}

void HostedCLAPEditor::onAttached()
{
    if (impl->attached)
        return;

    auto& container = static_cast<NativeViewContainer&>(view());

    // The earliest a Windows child window exists: it needs its parent first.
    auto* parent = container.getNativeParentHandle();

    if (parent == nullptr || !impl->createGui())
        return;

    impl->setScale(Impl::platformScale(&container, parent));

    // get_size, and the host asked to follow before the GUI is parented.
    if (auto reported = impl->reportedSize();
        reported && !sameSize(*reported, boundsOf(container)))
        requestOwnResize(*reported);

    if (!impl->alive())
        return;

    auto window = CLAPEditorPlatform::windowFor(parent);
    auto parented =
        impl->callGui([&] { return impl->gui->set_parent(impl->clap, &window); });

    // Lost from inside set_parent: the deferred closed destroys it.
    if (!impl->alive())
        return;

    if (!parented)
    {
        impl->destroyGui();
        return;
    }

    impl->nativeParent = parent;
    impl->attached = true;

    // The size the host settled on, when it is not the plugin's.
    auto settled = boundsOf(container);
    impl->lastSize = settled;
    impl->followHost(settled);

    if (!impl->alive())
        return;

    auto showed = impl->callGui([this] { return impl->gui->show(impl->clap); });

    if (showed && impl->alive())
        impl->shown = true;
}

void HostedCLAPEditor::onRemoved()
{
    impl->destroyGui();
    impl->nativeParent = nullptr;
}

} // namespace MakeASound::Hosting
