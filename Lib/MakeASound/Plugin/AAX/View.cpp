#include "View.h"
#include "Adapter.h"
#include "../Realtime/MessageThread.h"
#include "../UI/GenericEditor.h"

#include <AAX_IViewContainer.h>

#include <eacp/Graphics/View/View.h>
#include <eacp/Graphics/Window/EmbeddedView.h>

namespace MakeASound::AAX
{

namespace
{
AAX_Point pointFor(EditorSize size) noexcept
{
    return {static_cast<float>(size.height), static_cast<float>(size.width)};
}
} // namespace

bool hasAAXView() noexcept
{
    return true;
}

AAX_IEffectGUI* AAX_CALLBACK createAAXView()
{
    return new View;
}

View::View() = default;

// The base destructor's Uninitialize can no longer reach DeleteViewContainer.
View::~View()
{
    View::Uninitialize();
}

AAX_Result View::Initialize(IACFUnknown* controller)
{
    adoptHostMessageThread();
    return AAX_CEffectGUI::Initialize(controller);
}

// The editor goes before the base releases the data model that owns its plugin.
AAX_Result View::Uninitialize()
{
    if (GetViewContainer() != nullptr)
        SetViewContainer(nullptr);

    shownEditor.reset();
    return AAX_CEffectGUI::Uninitialize();
}

Plugin* View::findPlugin()
{
    auto* adapter = dynamic_cast<Adapter*>(GetEffectParameters());
    auto* wrapper = adapter != nullptr ? adapter->wrapper() : nullptr;
    return wrapper != nullptr ? &wrapper->plugin() : nullptr;
}

void View::CreateViewContents()
{
    if (shownEditor)
        return;

    auto* plugin = findPlugin();

    if (plugin == nullptr)
        return;

    auto created = plugin->createEditor();

    if (!created)
        created = EA::makeOwned<GenericEditor>(*plugin);

    size = created->constraints().constrain(created->initialSize());
    shownEditor = std::move(created);
}

void View::CreateViewContainer()
{
    CreateViewContents();

    auto* parent = GetViewContainerPtr();

    if (!shownEditor || embedded || parent == nullptr
        || GetViewContainerType() != nativeContainerType())
        return;

    embedded = EA::makeOwned<eacp::Graphics::EmbeddedView>(
        parent, eacp::Graphics::EmbeddedViewOptions {size.width, size.height});
    pinToTopLeft();
    embedded->setContentView(shownEditor->view());

    shownEditor->setHost(this);
    shownEditor->onAttached();
}

void View::DeleteViewContainer()
{
    if (!embedded)
        return;

    shownEditor->onRemoved();
    shownEditor->setHost(nullptr);
    embedded.reset();
}

AAX_Result View::GetViewSize(AAX_Point* viewSize) const
{
    if (viewSize == nullptr)
        return AAX_ERROR_NULL_ARGUMENT;

    if (!shownEditor)
        return AAX_ERROR_NULL_OBJECT;

    *viewSize = pointFor(size);
    return AAX_SUCCESS;
}

bool View::resizeEditor(EditorSize requested)
{
    auto* container = GetViewContainer();

    if (!shownEditor || container == nullptr)
        return false;

    auto constrained = shownEditor->constraints().constrain(requested);
    auto point = pointFor(constrained);

    if (container->SetViewSize(point) != AAX_SUCCESS)
        return false;

    applySize(constrained);
    return true;
}

void View::applySize(EditorSize newSize)
{
    size = newSize;

    if (embedded)
        embedded->setSize(size.width, size.height);
}

} // namespace MakeASound::AAX
