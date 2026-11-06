#include "AAXValidator.h"

namespace MakeASound::Pluginval
{
Result validateAAX(const eacp::FilePath& bundle, const Options&)
{
    return {.bundle = bundle, .log = "AAX validation is macOS only\n"};
}
} // namespace MakeASound::Pluginval
