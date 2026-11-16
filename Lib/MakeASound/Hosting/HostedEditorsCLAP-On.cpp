#include "HostedEditorsCLAP.h"
#include "CLAP/HostedCLAPEditor.h"

namespace MakeASound::Hosting
{

OwningPointer<Editor> hostedCLAPEditor(HostedPlugin& plugin)
{
    auto* clap = dynamic_cast<CLAPHostedPlugin*>(&plugin);
    return clap != nullptr ? HostedCLAPEditor::create(*clap)
                           : OwningPointer<Editor>();
}

} // namespace MakeASound::Hosting
