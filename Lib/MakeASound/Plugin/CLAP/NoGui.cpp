#include "Gui.h"

namespace MakeASound::CLAP
{

// No create without a gui extension, so there is never a view to delete.
void GuiViewDeleter::operator()(GuiView*) const noexcept
{
}

const void* guiExtension() noexcept
{
    return nullptr;
}

void releaseViewDevices()
{
}

} // namespace MakeASound::CLAP
