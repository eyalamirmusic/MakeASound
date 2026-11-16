#include "GuiView.h"
#include "Adapter.h"
#include "../UI/GenericEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace MakeASound::CLAP
{

namespace
{
int toInt(uint32_t value) noexcept
{
    return static_cast<int>(
        std::min(value, static_cast<uint32_t>(std::numeric_limits<int>::max())));
}

uint32_t toUnsigned(int value) noexcept
{
    return static_cast<uint32_t>(std::max(0, value));
}

bool isNativeApi(const char* api) noexcept
{
    return api != nullptr && std::strcmp(api, GuiView::nativeApi()) == 0;
}

GuiView* viewOf(const clap_plugin* plugin) noexcept
{
    return Adapter::from(plugin)->guiView().get();
}

// Named apart from Adapter.cpp's and Params.cpp's callbacks, which share a unity
// batch with these.
namespace GuiCalls
{
bool isApiSupported(const clap_plugin*, const char* api, bool isFloating) noexcept
{
    return !isFloating && isNativeApi(api);
}

bool getPreferredApi(const clap_plugin*, const char** api, bool* isFloating) noexcept
{
    if (api == nullptr || isFloating == nullptr)
        return false;

    *api = GuiView::nativeApi();
    *isFloating = false;
    return true;
}

// The plugin's own editor, or GenericEditor when it has none of its own, as the
// VST3 view shows.
bool create(const clap_plugin* clap, const char* api, bool isFloating) noexcept
{
    auto& adapter = *Adapter::from(clap);

    if (isFloating || !isNativeApi(api) || adapter.guiView() != nullptr)
        return false;

    try
    {
        auto& plugin = adapter.plugin();

        if (!plugin.hasEditor())
            return false;

        auto editor = plugin.createEditor();

        if (!editor)
            editor = EA::makeOwned<GenericEditor>(plugin);

        adapter.guiView().reset(new GuiView(adapter.clapHost(), std::move(editor)));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void destroy(const clap_plugin* clap) noexcept
{
    Adapter::from(clap)->guiView().reset();
}

bool setScale(const clap_plugin* clap, double scale) noexcept
{
    auto* view = viewOf(clap);
    return view != nullptr && view->setScale(scale);
}

bool getSize(const clap_plugin* clap, uint32_t* width, uint32_t* height) noexcept
{
    auto* view = viewOf(clap);

    if (view == nullptr || width == nullptr || height == nullptr)
        return false;

    try
    {
        return view->getSize(*width, *height);
    }
    catch (...)
    {
        return false;
    }
}

bool canResize(const clap_plugin* clap) noexcept
{
    auto* view = viewOf(clap);

    try
    {
        return view != nullptr && view->canResize();
    }
    catch (...)
    {
        return false;
    }
}

bool getResizeHints(const clap_plugin* clap, clap_gui_resize_hints* hints) noexcept
{
    auto* view = viewOf(clap);

    try
    {
        return view != nullptr && hints != nullptr && view->resizeHints(*hints);
    }
    catch (...)
    {
        return false;
    }
}

bool adjustSize(const clap_plugin* clap, uint32_t* width, uint32_t* height) noexcept
{
    auto* view = viewOf(clap);

    if (view == nullptr || width == nullptr || height == nullptr)
        return false;

    try
    {
        return view->adjustSize(*width, *height);
    }
    catch (...)
    {
        return false;
    }
}

bool setSize(const clap_plugin* clap, uint32_t width, uint32_t height) noexcept
{
    auto* view = viewOf(clap);

    try
    {
        return view != nullptr && view->setSize(width, height);
    }
    catch (...)
    {
        return false;
    }
}

bool setParent(const clap_plugin* clap, const clap_window* window) noexcept
{
    auto* view = viewOf(clap);

    try
    {
        return view != nullptr && window != nullptr && view->setParent(*window);
    }
    catch (...)
    {
        return false;
    }
}

// Floating windows are not offered, so there is nothing to keep above.
bool setTransient(const clap_plugin*, const clap_window*) noexcept
{
    return false;
}

void suggestTitle(const clap_plugin*, const char*) noexcept
{
}

bool show(const clap_plugin* clap) noexcept
{
    auto* view = viewOf(clap);
    return view != nullptr && view->setVisible(true);
}

bool hide(const clap_plugin* clap) noexcept
{
    auto* view = viewOf(clap);
    return view != nullptr && view->setVisible(false);
}

constexpr auto extension = clap_plugin_gui {isApiSupported,
                                            getPreferredApi,
                                            create,
                                            destroy,
                                            setScale,
                                            getSize,
                                            canResize,
                                            getResizeHints,
                                            adjustSize,
                                            setSize,
                                            setParent,
                                            setTransient,
                                            suggestTitle,
                                            show,
                                            hide};
} // namespace GuiCalls
} // namespace

const void* guiExtension() noexcept
{
    return &GuiCalls::extension;
}

void releaseViewDevices()
{
    releaseEmbeddedEditorDevices();
}

void GuiViewDeleter::operator()(GuiView* view) const noexcept
{
    delete view;
}

GuiView::GuiView(const clap_host* hostToUse, OwningPointer<Editor> editorToShow)
    : host(hostToUse)
    , embedding(std::move(editorToShow), *this)
{
    if (host != nullptr && host->get_extension != nullptr)
        hostGui = static_cast<const clap_host_gui*>(
            host->get_extension(host, CLAP_EXT_GUI));

    size = embedding.openingSize();
}

GuiView::~GuiView()
{
    embedding.detach();
}

bool GuiView::applyHostScale(double scale) noexcept
{
    if (!(scale > 0.0) || !std::isfinite(scale))
        return false;

    auto points = embedding.toPoints(size);
    embedding.setHostScale(static_cast<float>(scale));
    size = embedding.toUnits(points);
    return true;
}

// Until the host sets a size, the opening size, in units at the current scale.
bool GuiView::getSize(uint32_t& width, uint32_t& height)
{
    if (!sizedByHost)
        size = embedding.openingSize();

    width = toUnsigned(size.width);
    height = toUnsigned(size.height);
    return true;
}

bool GuiView::canResize()
{
    return embedding.resizable();
}

// The ratio is a proportion, the same in points and in pixels.
bool GuiView::resizeHints(clap_gui_resize_hints& hints)
{
    auto limits = embedding.constraints();
    auto ratio = limits.aspectRatio;
    auto keepsRatio = limits.resizable && ratio.width > 0 && ratio.height > 0;

    hints.can_resize_horizontally = limits.resizable;
    hints.can_resize_vertically = limits.resizable;
    hints.preserve_aspect_ratio = keepsRatio;
    hints.aspect_ratio_width = keepsRatio ? toUnsigned(ratio.width) : 0;
    hints.aspect_ratio_height = keepsRatio ? toUnsigned(ratio.height) : 0;
    return true;
}

bool GuiView::adjustSize(uint32_t& width, uint32_t& height)
{
    if (!embedding.resizable())
        return false;

    auto allowed = embedding.constrain({toInt(width), toInt(height)});
    width = toUnsigned(allowed.width);
    height = toUnsigned(allowed.height);
    return true;
}

// Whatever the host settles on, as the VST3 view's onSize: adjust_size is where
// the policy applies.
bool GuiView::setSize(uint32_t width, uint32_t height)
{
    applySize({toInt(width), toInt(height)});
    return true;
}

void GuiView::applySize(EditorSize units)
{
    sizedByHost = true;
    ++sizeChanges;
    size = units;
    embedding.setSize(units);
}

bool GuiView::setParent(const clap_window& window)
{
    if (!isNativeApi(window.api))
        return false;

    auto* parent = nativeParentOf(window);

    if (parent == nullptr)
        return false;

    auto width = uint32_t {0};
    auto height = uint32_t {0};
    getSize(width, height);

    embedding.attach(parent, size);

    if (!visible)
        embedding.setVisible(false);

    return true;
}

// Before set_parent the choice waits for the surface.
bool GuiView::setVisible(bool shouldBeVisible)
{
    visible = shouldBeVisible;
    embedding.setVisible(shouldBeVisible);
    return true;
}

// The host need not call set_size for a request it accepts, so one it did not
// answer is applied here; a refused one leaves the size as it was.
bool GuiView::resizeEditor(EditorSize points)
{
    if (hostGui == nullptr || hostGui->request_resize == nullptr)
        return false;

    auto requested = embedding.toUnits(points);
    auto changesBefore = sizeChanges;

    if (!hostGui->request_resize(
            host, toUnsigned(requested.width), toUnsigned(requested.height)))
        return false;

    if (sizeChanges == changesBefore)
        applySize(requested);

    return true;
}

bool GuiView::unhandledKey(const eacp::Graphics::KeyEvent& key)
{
    return embedding.unhandledKey(key);
}

} // namespace MakeASound::CLAP
