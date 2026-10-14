#include "WorkerProcess.h"

namespace MakeASound::Hosting
{

// A crashing process ends without a prompt: ReportCrash writes its report and
// asks nothing of a background process.
void prepareWorkerProcess() noexcept
{
}

} // namespace MakeASound::Hosting
