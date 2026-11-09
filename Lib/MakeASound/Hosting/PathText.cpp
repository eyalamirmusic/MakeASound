#include "PathText.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/StdPath.h>

namespace MakeASound::Hosting
{

std::string pathText(const std::filesystem::path& path)
{
    return eacp::FilePath(path).str();
}

std::filesystem::path stdPath(std::string_view text)
{
    return eacp::toStdPath(eacp::FilePath(text));
}

} // namespace MakeASound::Hosting
