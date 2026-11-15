#include "../Plugin/VST3/VST3Common.h"
#include "HostedEditor.h"
#include "VST3/HostedVST3Editor.h"
#include "VST3/VST3HostedPlugin.h"

namespace MakeASound::Hosting
{

HostedEditorFactory hostedEditors()
{
    return [](HostedPlugin& plugin) -> OwningPointer<Editor>
    {
        auto* vst3 = dynamic_cast<VST3HostedPlugin*>(&plugin);

        if (vst3 == nullptr || vst3->editController() == nullptr)
            return {};

        return HostedVST3Editor::create(
            vst3->editController()->createView(Steinberg::Vst::ViewType::kEditor));
    };
}

} // namespace MakeASound::Hosting
