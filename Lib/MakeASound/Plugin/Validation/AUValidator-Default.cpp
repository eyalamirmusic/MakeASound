#include "AUValidator.h"

namespace MakeASound::Pluginval
{
Result validateAudioUnit(const eacp::FilePath& bundle, const Options&)
{
    return {.bundle = bundle, .log = "AU validation is macOS only\n"};
}
} // namespace MakeASound::Pluginval
