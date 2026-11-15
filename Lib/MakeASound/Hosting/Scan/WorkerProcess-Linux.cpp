#include "WorkerProcess.h"

#include <sys/resource.h>

namespace MakeASound::Hosting
{

// A crash already ends the process without a prompt. No core file, though: a
// plugin that crashes its scan is an answer, and a sweep can meet many of them.
void prepareWorkerProcess() noexcept
{
    const auto noCoreFile = rlimit {0, 0};
    setrlimit(RLIMIT_CORE, &noCoreFile);
}

} // namespace MakeASound::Hosting
