#include "ExecutablePath.h"

#include <eacp/Core/App/AppEnvironment.h>

#include <filesystem>

namespace MakeASound::Hosting
{

std::string currentExecutablePath()
{
    namespace fs = std::filesystem;

    const auto& arguments = eacp::Apps::getAppEnvironment().commandLineArgs;

    const auto own = arguments.size() > 0 && !arguments[0].empty()
                         ? arguments[0]
                         : executablePathFromPlatform();

    const auto asGiven = fs::path(own);

    // A bare name came off PATH; see the header for why it stays bare.
    if (!asGiven.has_parent_path())
        return own;

    auto ec = std::error_code();
    const auto absolute = fs::absolute(asGiven, ec);

    return ec ? own : absolute.string();
}

} // namespace MakeASound::Hosting
