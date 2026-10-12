// A failed assert() or CRT check in a Debug build on Windows reports through a
// message box by default, which blocks the test process until someone clicks it.
// Before any test runs, send those reports to stderr and let abort() end the
// process, and keep the OS from opening its own fault dialog as well.

#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
struct ReportToStderr
{
    ReportToStderr()
    {
        for (auto type: {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
        {
            _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
        }

        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    }
};

const ReportToStderr reportToStderr;
} // namespace
