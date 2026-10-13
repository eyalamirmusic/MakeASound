#pragma once

#include "AAXCommon.h"
#include "../Core/Editor.h"
#include "../Core/Plugin.h"
#include "../../Common/Common.h"

#include <AAX_CEffectGUI.h>
#include <AAX_GUITypes.h>

namespace eacp::Graphics
{
class EmbeddedView;
}

namespace MakeASound::AAX
{

// The editor's AAX_IEffectGUI, defined by View.cpp where the UI tier exists and
// by NoView.cpp where it does not; without one no GUI proc is registered, and
// Pro Tools shows its own controls.
bool hasAAXView() noexcept;
AAX_IEffectGUI* AAX_CALLBACK createAAXView();

// Pro Tools' window onto a plugin's Editor (createEditor(), or GenericEditor when
// null), embedded through eacp in the native view the host's container hands
// over, and the editor's EditorHost while it is embedded: a resize request goes
// to the container's SetViewSize. The editor lives as long as the view, the
// embedding only while the host has a container set. The platform's half is in
// View-<platform>: the container type it accepts and how the surface is pinned
// to the container's top-left. Message thread.
class View
    : public AAX_CEffectGUI
    , public EditorHost
{
public:
    View();
    ~View() override;

    AAX_Result Initialize(IACFUnknown* controller) override;
    AAX_Result Uninitialize() override;
    AAX_Result GetViewSize(AAX_Point* size) const override;

    bool resizeEditor(EditorSize size) override;
    bool drawsResizeHandle() const override { return false; }

    Editor* editor() noexcept { return shownEditor.get(); }

protected:
    // The plugin behind GetEffectParameters(), null until its EffectInit ran.
    virtual Plugin* findPlugin();

    void CreateViewContents() override;
    void CreateViewContainer() override;
    void DeleteViewContainer() override;

private:
    static AAX_EViewContainer_Type nativeContainerType() noexcept;
    // Keeps the surface at the container's top-left at the size last set,
    // whichever way up the host measures and however it resizes its view.
    void pinToTopLeft() noexcept;
    void applySize(EditorSize newSize);

    OwningPointer<Editor> shownEditor;
    OwningPointer<eacp::Graphics::EmbeddedView> embedded;
    EditorSize size;
};

} // namespace MakeASound::AAX
