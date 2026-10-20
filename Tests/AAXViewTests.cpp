#include "TestPlugins.h"
#include "TestEditor.h"

#include <MakeASound/Plugin/AAX/View.h>
#include <MakeASound/Plugin/UI/GenericEditor.h>

#include <AAX_IACFViewContainer.h>
#include <AAX_UIDs.h>
#include <acfextras.h>

#include <eacp/Graphics/View/View.h>

#include <NanoTest/NanoTest.h>

namespace AAXViewTests
{
namespace
{
using namespace nano;
using namespace TestPlugins;

// The controller a GUI is initialized with; it offers no interface, so the
// view's data model is whatever findPlugin answers.
struct NoInterfaceController final : IACFUnknown
{
    ACFRESULT ACFMETHODCALLTYPE QueryInterface(const acfIID&, void** out) override
    {
        *out = nullptr;
        return ACF_E_NOINTERFACE;
    }

    acfUInt32 ACFMETHODCALLTYPE AddRef() override { return 1; }
    acfUInt32 ACFMETHODCALLTYPE Release() override { return 1; }
};

// Pro Tools' side of the window: a native view to embed in and a record of the
// sizes the plugin asked for, each accepted or refused.
struct FakeViewContainer final : AAX_IACFViewContainer
{
    ACFRESULT ACFMETHODCALLTYPE QueryInterface(const acfIID& iid,
                                               void** out) override
    {
        if (iid == IID_IAAXViewContainerV1)
        {
            *out = static_cast<AAX_IACFViewContainer*>(this);
            return ACF_OK;
        }

        *out = nullptr;
        return ACF_E_NOINTERFACE;
    }

    acfUInt32 ACFMETHODCALLTYPE AddRef() override { return 1; }
    acfUInt32 ACFMETHODCALLTYPE Release() override { return 1; }

    int32_t GetType() override { return type; }
    void* GetPtr() override { return parent.getHandle(); }
    AAX_Result GetModifiers(uint32_t* modifiers) override
    {
        *modifiers = 0;
        return AAX_SUCCESS;
    }

    AAX_Result SetViewSize(AAX_Point& size) override
    {
        requests.push_back(
            {static_cast<int>(size.horz), static_cast<int>(size.vert)});
        return accept ? AAX_SUCCESS : AAX_ERROR_UNIMPLEMENTED;
    }

    AAX_Result HandleParameterMouseDown(AAX_CParamID, uint32_t) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result HandleParameterMouseDrag(AAX_CParamID, uint32_t) override
    {
        return AAX_SUCCESS;
    }

    AAX_Result HandleParameterMouseUp(AAX_CParamID, uint32_t) override
    {
        return AAX_SUCCESS;
    }

    eacp::Graphics::View parent;
    int32_t type = MakeASound::AAX::View::nativeContainerType();
    bool accept = true;
    std::vector<EditorSize> requests;
};

// The GUI over a plugin of the test's own, standing in for the data model.
struct PluginView final : MakeASound::AAX::View
{
    explicit PluginView(Plugin& pluginToShow)
        : plugin(pluginToShow)
    {
        Initialize(&controller);
    }

    Plugin* findPlugin() override { return &plugin; }

    EditorSize size() const
    {
        auto point = AAX_Point {};
        GetViewSize(&point);
        return {static_cast<int>(point.horz), static_cast<int>(point.vert)};
    }

    Plugin& plugin;
    NoInterfaceController controller;
};

struct ScopedAAXEditor
{
    explicit ScopedAAXEditor(EditorConstraints limits = {},
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

    ~ScopedAAXEditor() { Module::editorFactory = nullptr; }

    ScopedAAXEditor(const ScopedAAXEditor&) = delete;
    ScopedAAXEditor& operator=(const ScopedAAXEditor&) = delete;

    TestEditor* editor = nullptr;
};

EditorConstraints boundedAAX()
{
    return {.resizable = true, .minimum = {200, 100}, .maximum = {800, 600}};
}

bool isSize(EditorSize size, int width, int height)
{
    return size.width == width && size.height == height;
}

auto tHasView =
    test("AAX/theModuleHasAView") = [] { check(MakeASound::AAX::hasAAXView()); };

auto tSize = test("AAX/theViewAnswersTheEditorsConstrainedSize") = []
{
    auto plugin = GainPlugin {};

    {
        auto scoped = ScopedAAXEditor {boundedAAX(), {1000, 50}};
        auto view = PluginView(plugin);
        check(scoped.editor != nullptr);
        check(isSize(view.size(), 800, 100));
    }

    auto view = PluginView(plugin);
    check(dynamic_cast<GenericEditor*>(view.editor()) != nullptr);

    auto generic = GenericEditor(plugin);
    auto expected = generic.constraints().constrain(generic.initialSize());
    check(isSize(view.size(), expected.width, expected.height));
};

auto tResize = test("AAX/requestResizeGoesThroughTheViewContainer") = []
{
    auto plugin = GainPlugin {};
    auto scoped = ScopedAAXEditor {boundedAAX()};
    auto view = PluginView(plugin);
    auto container = FakeViewContainer {};

    check(!scoped.editor->requestResize({500, 500}));

    view.SetViewContainer(&container);
    check(scoped.editor->requestResize({1000, 450}));
    check(container.requests.size() == 1);
    check(isSize(container.requests.back(), 800, 450));
    check(isSize(view.size(), 800, 450));

    container.accept = false;
    check(!scoped.editor->requestResize({300, 300}));
    check(container.requests.size() == 2);
    check(isSize(view.size(), 800, 450));

    view.SetViewContainer(nullptr);
    check(!scoped.editor->requestResize({300, 300}));
    check(container.requests.size() == 2);
};

auto tFixed = test("AAX/aFixedEditorCannotAskTheContainer") = []
{
    auto plugin = GainPlugin {};
    auto scoped = ScopedAAXEditor {};
    auto view = PluginView(plugin);
    auto container = FakeViewContainer {};
    view.SetViewContainer(&container);

    check(!scoped.editor->canRequestResize());
    check(!scoped.editor->requestResize({500, 500}));
    check(container.requests.empty());
    check(isSize(view.size(), 400, 300));
};

// Records whether the view had cleared the host by the time it destroyed us.
struct AAXTeardownEditor final : Editor
{
    explicit AAXTeardownEditor(bool& clearedToSet)
        : cleared(clearedToSet)
    {
    }

    ~AAXTeardownEditor() override { cleared = host() == nullptr; }

    eacp::Graphics::View& view() override { return inner.view(); }

    bool& cleared;
    TestEditor inner;
};

auto tHost = test("AAX/theViewIsTheEditorsHostWhileEmbedded") = []
{
    auto plugin = GainPlugin {};

    {
        auto scoped = ScopedAAXEditor {boundedAAX()};
        auto view = PluginView(plugin);
        auto* asHost = static_cast<EditorHost*>(&view);
        auto container = FakeViewContainer {};

        check(scoped.editor->host() == nullptr);

        view.SetViewContainer(&container);
        check(scoped.editor->host() == asHost);
        check(!scoped.editor->host()->drawsResizeHandle());
        check(scoped.editor->attached == 1);
        check(scoped.editor->hostAtAttach == asHost);

        view.SetViewContainer(nullptr);
        check(scoped.editor->removed == 1);
        check(scoped.editor->hostAtRemove == asHost);
        check(scoped.editor->host() == nullptr);

        view.SetViewContainer(&container);
        check(scoped.editor->attached == 2);
        check(scoped.editor->host() == asHost);
        view.SetViewContainer(nullptr);
    }

    auto cleared = false;
    Module::editorFactory = [&cleared](Plugin&)
    { return OwningPointer<Editor>(EA::makeOwned<AAXTeardownEditor>(cleared)); };

    auto container = FakeViewContainer {};

    {
        auto view = PluginView(plugin);
        Module::editorFactory = nullptr;
        view.SetViewContainer(&container);
    }

    check(cleared);
};

auto tWrongType = test("AAX/aForeignContainerTypeEmbedsNothing") = []
{
    auto plugin = GainPlugin {};
    auto scoped = ScopedAAXEditor {boundedAAX()};
    auto view = PluginView(plugin);
    auto container = FakeViewContainer {};
    // A UIView is no desktop platform's native container.
    container.type = AAX_eViewContainer_Type_UIView;

    view.SetViewContainer(&container);
    check(scoped.editor->host() == nullptr);
    check(scoped.editor->attached == 0);
    view.SetViewContainer(nullptr);
};

} // namespace
} // namespace AAXViewTests
