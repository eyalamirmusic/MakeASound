#include "../Plugin/VST3/VST3Common.h"
#include "HostedEditor.h"
#include "AU/HostedAudioUnit.h"
#include "AU/HostedAudioUnitEditor.h"
#include "VST3/HostedVST3Editor.h"
#include "VST3/VST3HostedPlugin.h"

namespace MakeASound::Hosting
{

HostedEditorFactory hostedEditors()
{
    return [](HostedPlugin& plugin) -> OwningPointer<Editor>
    {
        if (auto* vst3 = dynamic_cast<VST3HostedPlugin*>(&plugin))
        {
            auto* controller = vst3->editController();

            if (controller == nullptr)
                return {};

            return HostedVST3Editor::create(
                controller->createView(Steinberg::Vst::ViewType::kEditor));
        }

        if (auto* unit = dynamic_cast<HostedAudioUnit*>(&plugin))
            return EA::makeOwned<HostedAudioUnitEditor>(unit->audioUnit());

        return {};
    };
}

} // namespace MakeASound::Hosting
