#include "WorkerProcess.h"

#include <cstdlib>

#include <windows.h>

namespace MakeASound::Hosting
{

void prepareWorkerProcess() noexcept
{
    // No critical-error or fault dialog, inherited by nothing else since the
    // worker spawns nothing.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX
                 | SEM_NOOPENFILEERRORBOX);

    // abort() neither shows its message box nor calls Windows Error Reporting.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

} // namespace MakeASound::Hosting
