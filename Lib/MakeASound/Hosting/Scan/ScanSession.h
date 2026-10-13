#pragma once

#include "ScanCoordinator.h"
#include "../PluginCache.h"

#include <cstdint>
#include <functional>
#include <string>

namespace MakeASound::Hosting
{

// One whole SWEEP: find every plugin installed, scan those the cache cannot
// already answer for (each in a worker process), put the results in the database
// and write it back.
//
// It runs in the host's own process and reports through ordinary callbacks; only
// loading a plugin, the part that can crash, happens in a child. Like the
// coordinator underneath, it is a message-thread object with no thread of its
// own: a console tool pumps poll(), a GUI app calls setAutoPoll(true).
//
//     auto formats = FormatManager();
//     formats.addDefaultFormats();
//
//     auto session = ScanSession(formats);
//     session.onTargetFinished = [](int done, int total, const ModuleScan& scan)
//     { showProgress(done, total, scan); };
//     session.setAutoPoll(true);
//     session.start();
//
// Which formats a sweep covers is the FormatManager's: every format it holds,
// unless ScanSessionOptions::formats narrows it.

struct ScanSessionOptions
{
    // The database to read and write. Empty means this app's own,
    // defaultPluginCachePath().
    std::string cachePath;

    // Extra folders to look in, and whether each format's own folders are looked
    // in as well.
    Vector<std::string> searchFolders;
    bool useDefaultFolders = true;

    // The formats to sweep. Empty means every format the FormatManager holds. A
    // format left out is not looked for, and its cached entries are left alone.
    Vector<PluginFormat> formats;

    // Throw the database away and scan everything. Off by default: a sweep is
    // incremental, scanning only what is new or changed. This is the answer to a
    // database gone wrong, including a plugin blacklisted for a crash it would no
    // longer repeat.
    bool forceRescan = false;

    // Workers in flight (zero: defaultScanJobCount()) and how long one may take
    // (zero or less: no watchdog).
    int jobs = 0;
    int timeoutMs = 30000;

    // How each target is scanned, and what to launch when that is a child: by
    // default this executable relaunched with --makeasound-scan. A host that
    // ships a separate scanner passes workerCommandFor(its path).
    ScanMode mode = ScanMode::Subprocess;
    WorkerCommand worker = defaultWorkerCommand();
};

// How many targets one format contributed to a sweep.
struct ScanFormatCount
{
    PluginFormat format = PluginFormat::Unknown;
    int targets = 0;
};

// What the sweep found, before it loaded a single plugin.
struct ScanSessionPlan
{
    // Everything found, what the cache already answers, and the difference,
    // which is what gets scanned and what onTargetFinished's `total` counts.
    int found = 0;
    int cached = 0;
    int toScan = 0;

    // What each swept format found, in the FormatManager's order, and how many
    // folders were walked.
    Vector<ScanFormatCount> formats;
    int folders = 0;

    // The resolved settings: jobs with its default filled in, and the database
    // this run writes.
    int jobs = 0;
    int timeoutMs = 0;
    std::string cachePath;

    // Set when forceRescan could NOT delete the old database. The sweep carries
    // on and overwrites it at the end, but a front end may want to say so.
    bool couldNotRemoveDatabase = false;

    // The targets `format` contributed, zero when it was not swept.
    int targetsOf(PluginFormat format) const noexcept;
};

// How the sweep ended.
struct ScanSessionSummary
{
    // Drawn from what was SCANNED (the plan's toScan). `failed` answered with an
    // error; `crashed` never answered.
    int scanned = 0;
    int ok = 0;
    int failed = 0;
    int crashed = 0;

    std::int64_t elapsedMs = 0;

    // The database written, and whether writing it worked.
    std::string cachePath;
    bool saved = false;

    // Set when the run was cancel()ed. What was scanned by then is still saved.
    bool cancelled = false;
};

class ScanSession
{
public:
    // `formats` must outlive the session.
    explicit ScanSession(const FormatManager& formats,
                         ScanSessionOptions optionsToUse = {});
    ~ScanSession();

    ScanSession(const ScanSession&) = delete;
    ScanSession& operator=(const ScanSession&) = delete;

    const ScanSessionOptions& options() const;

    // The database this sweep reads and fills, valid for the session's lifetime,
    // so a UI can list results as they land.
    const PluginCache& cache() const;

    // Discovers, plans and starts scanning. onStarted fires from inside; a sweep
    // the cache answers completely also saves and fires onFinished from inside. A
    // sweep in flight is cancelled first, silently.
    void start();

    void poll();

    // Stops the sweep: workers killed, unscanned targets dropped, what was
    // scanned saved, and onFinished fired with `cancelled` set.
    void cancel();

    bool isRunning() const;

    void setAutoPoll(bool shouldAutoPoll);
    void setPollIntervalMs(int intervalToUse);

    // Callbacks, all on the calling thread.
    std::function<void(const ScanSessionPlan&)> onStarted;

    // One per target scanned, in the order they landed. `index` is 1-based and
    // `total` is the plan's toScan.
    std::function<void(int index, int total, const ModuleScan& scan)>
        onTargetFinished;

    // Once, after the database was written (or failed to be).
    std::function<void(const ScanSessionSummary&)> onFinished;

private:
    struct Impl;
    OwningPointer<Impl> impl;
};

} // namespace MakeASound::Hosting
