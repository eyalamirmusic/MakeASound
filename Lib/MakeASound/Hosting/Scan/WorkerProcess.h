#pragma once

namespace MakeASound::Hosting
{

// Readies this process to be a scan worker, just before it loads the plugin. A
// worker that crashes must simply end, since the host reads the exit as the
// answer: on Windows that means no fault dialog, no abort() message box and no
// "the application has stopped working" prompt, any of which would sit there
// until the host's timeout killed it. One TU per platform
// (WorkerProcess-<os>.cpp); nothing to do on macOS.
void prepareWorkerProcess() noexcept;

} // namespace MakeASound::Hosting
