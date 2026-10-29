#pragma once

#include "WorkerCommand.h"
#include "../FormatManager.h"

#include <functional>
#include <string>

namespace MakeASound::Hosting
{

// How a target gets scanned.
enum class ScanMode
{
    // One child process per target, the default and the only mode that is safe
    // against third-party code: the plugin is loaded inside a worker that may
    // crash or hang without taking the host with it.
    Subprocess,

    // FormatManager::scanTarget on a background thread of THIS process. No
    // isolation at all, but no spawn cost, and a debugger steps straight
    // through. For tests and for debugging a format.
    InProcess
};

// Scans a list of targets, N at a time, and reports each result as it lands.
//
// A plain message-thread object with no thread of its own: all its state moves
// inside poll(), and every callback fires from whoever called it. A console
// tool or a test drives poll() in a loop; a GUI app lets setAutoPoll(true) drive
// it from an eacp timer.
//
// Every target produces exactly one ModuleScan, whatever happened:
//
//   the command's decoder read a result  that result, verbatim
//   the worker exited non-zero           crashed
//   the worker outlived the timeout      killed, crashed
//   the decoder read nothing usable      crashed
//   the command has no decoder           crashed
//
// A synthesized failure carries the target's stamp and format, through the
// FormatManager, so the cache blacklists it until the plugin changes.
class ScanCoordinator
{
public:
    explicit ScanCoordinator(const FormatManager& formatsToUse);
    ~ScanCoordinator();

    ScanCoordinator(const ScanCoordinator&) = delete;
    ScanCoordinator& operator=(const ScanCoordinator&) = delete;

    // Configuration, all of it taking effect on the next start().
    void setMode(ScanMode modeToUse);
    ScanMode mode() const;

    void setWorkerCommand(WorkerCommand commandToUse);
    const WorkerCommand& workerCommand() const;

    // Workers running at once, at least 1. Defaults to defaultScanJobCount().
    void setMaxConcurrent(int maxConcurrentToUse);
    int maxConcurrent() const;

    // How long one target may take before its worker is killed; zero or less
    // disables the watchdog. Subprocess mode only: an in-process scan cannot be
    // interrupted. Defaults to 30 s.
    void setTimeoutMs(int timeoutToUse);
    int timeoutMs() const;

    // Reaps finished jobs, kills overdue ones and launches replacements. Cheap
    // and safe when nothing is running.
    void poll();

    // Drives poll() from an eacp timer on the message thread. Off by default: a
    // console tool or a test has no loop for the timer to tick on, and a timer
    // behind a test that polls itself would race it.
    void setAutoPoll(bool shouldAutoPoll);
    void setPollIntervalMs(int intervalToUse);

    // Starts a run over `targets`. A run in flight is stopped first, silently. An
    // empty list finishes at once: onProgress(0, 0), then onFinished(), both from
    // inside start().
    void start(const Vector<std::string>& targets);

    // Stops the run: workers are killed, queued targets dropped, and onFinished()
    // fires, so a UI re-enables its button in one place either way. Results
    // already reported stand. An in-process scan already running cannot be
    // interrupted, so cancel() waits for it.
    void cancel();

    bool isRunning() const;
    int completed() const;
    int total() const;

    // The targets being scanned right now, in the order they started: each from
    // just before its onTargetStarted until just before its onModuleScanned.
    // Empty when nothing runs.
    Vector<std::string> inFlightTargets() const;

    // Callbacks, all delivered from poll() (or start()/cancel()).
    // A worker (or thread) was just started for `target`.
    std::function<void(const std::string& target)> onTargetStarted;
    std::function<void(const ModuleScan&)> onModuleScanned;
    std::function<void(int done, int total)> onProgress;
    std::function<void()> onFinished;

private:
    struct Impl;
    OwningPointer<Impl> impl;
};

// The default pool size: this machine's hardware concurrency, clamped to 1..8.
// Each worker loads a whole plugin, so past a handful a run is bound by memory
// and disk rather than cores, and a 20-way pool only makes failures harder to read.
int defaultScanJobCount();

} // namespace MakeASound::Hosting
