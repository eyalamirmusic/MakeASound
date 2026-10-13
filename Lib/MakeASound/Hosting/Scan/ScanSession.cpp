#include "ScanSession.h"
#include "Scanner.h"

#include <chrono>

namespace MakeASound::Hosting
{

int ScanSessionPlan::targetsOf(PluginFormat format) const noexcept
{
    for (const auto& count: formats)
        if (count.format == format)
            return count.targets;

    return 0;
}

struct ScanSession::Impl
{
    Impl(ScanSession& ownerToUse,
         const FormatManager& formatsToUse,
         ScanSessionOptions optionsToUse)
        : owner(ownerToUse)
        , formats(formatsToUse)
        , options(std::move(optionsToUse))
        , cachePath(options.cachePath.empty() ? defaultPluginCachePath()
                                              : options.cachePath)
        , cache(cachePath)
        , coordinator(formatsToUse)
    {
        // Entries stamp by their format, so a cached Audio Unit is not stale
        // forever.
        useFormatStamps(cache, formats);

        coordinator.onModuleScanned = [this](const ModuleScan& scan)
        { deliver(scan); };

        // The coordinator announces a cancelled run as it does a finished one, so
        // both ends of a sweep arrive here.
        coordinator.onFinished = [this] { complete(); };
    }

    void start()
    {
        // A superseded sweep stops silently: clearing `running` first keeps the
        // coordinator's cancellation from completing it.
        running = false;
        coordinator.cancel();

        startedAt = std::chrono::steady_clock::now();
        okCount = 0;
        errorCount = 0;
        crashedCount = 0;
        completedCount = 0;
        cancelling = false;
        running = true;

        plan = ScanSessionPlan();
        collectTargets();
        prepareCache();

        plan.found = targets.size();
        plan.toScan = toScan.size();
        plan.cached = plan.found - plan.toScan;
        plan.folders = folders.size();
        plan.jobs = options.jobs > 0 ? options.jobs : defaultScanJobCount();
        plan.timeoutMs = options.timeoutMs;
        plan.cachePath = cachePath;

        coordinator.setMode(options.mode);
        coordinator.setWorkerCommand(options.worker);
        coordinator.setMaxConcurrent(plan.jobs);
        coordinator.setTimeoutMs(options.timeoutMs);

        if (owner.onStarted)
            owner.onStarted(plan);

        // An empty run finishes from inside here, which makes a fully cached
        // sweep one call that saves and reports.
        coordinator.start(toScan);
    }

    void cancel()
    {
        if (!running)
            return;

        cancelling = true;
        coordinator.cancel();
    }

    bool sweeps(PluginFormat format) const
    {
        return options.formats.empty() || options.formats.contains(format);
    }

    void collectTargets()
    {
        targets = Vector<std::string>();
        folders = Vector<std::string>();
        swept = Vector<PluginFormat>();

        // The folders are recorded in the database as where this sweep looked,
        // whether or not anything walks them.
        for (auto i = 0; i < formats.size(); ++i)
            if (options.useDefaultFolders && sweeps(formats[i].format()))
                for (const auto& folder: formats[i].defaultSearchFolders())
                    folders.addIfNotThere(folder);

        for (const auto& folder: options.searchFolders)
            folders.addIfNotThere(folder);

        for (auto i = 0; i < formats.size(); ++i)
        {
            const auto& format = formats[i];

            if (!sweeps(format.format()))
                continue;

            swept.add(format.format());

            auto count = ScanFormatCount();
            count.format = format.format();

            for (const auto& target: format.findTargets(folders))
            {
                // A target two formats would both find belongs to the one that
                // claims it, so it is scanned, and counted, once.
                if (formats.findForTarget(target) != &format)
                    continue;

                if (targets.addIfNotThere(target))
                    ++count.targets;
            }

            plan.formats.add(count);
        }
    }

    void prepareCache()
    {
        if (options.forceRescan)
        {
            // Deleted before a single plugin is loaded, so a forced run that is
            // interrupted leaves no database rather than the stale one it was
            // asked to replace.
            plan.couldNotRemoveDatabase = !cache.clear();
        }
        else
        {
            // A missing or corrupt file reads as empty: scan everything.
            cache.load();
        }

        toScan = cache.modulesNeedingScan(targets);
    }

    void deliver(const ModuleScan& scan)
    {
        auto result = scan;

        // A worker's own answer carries its format; this keeps a result the
        // coordinator synthesized, or a worker of another build, consistent with
        // how the cache stamps it.
        result.format = formats.formatOfTarget(result.path);

        if (result.crashed)
            ++crashedCount;
        else if (!result.error.empty())
            ++errorCount;
        else
            ++okCount;

        ++completedCount;

        // Applied as it lands, so a UI reading the cache is never behind the
        // progress it shows.
        cache.apply(result);

        if (owner.onTargetFinished)
            owner.onTargetFinished(completedCount, plan.toScan, result);
    }

    void complete()
    {
        if (!running)
            return;

        running = false;

        // Entries this sweep did not find go, so an uninstalled plugin leaves the
        // database. Only for the formats it swept: a format it never looked for
        // has nothing to say about its cached entries.
        for (const auto& format: swept)
            cache.removeMissing(targets, format);

        cache.setSearchPaths(folders);

        auto summary = ScanSessionSummary();

        summary.scanned = completedCount;
        summary.ok = okCount;
        summary.failed = errorCount;
        summary.crashed = crashedCount;
        summary.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startedAt)
                                .count();
        summary.cachePath = cachePath;
        summary.saved = cache.save();
        summary.cancelled = cancelling;

        if (owner.onFinished)
            owner.onFinished(summary);
    }

    ScanSession& owner;
    const FormatManager& formats;
    ScanSessionOptions options;

    // Declared before `cache`, which is constructed from it.
    std::string cachePath;
    PluginCache cache;
    ScanCoordinator coordinator;

    Vector<std::string> folders;
    Vector<std::string> targets;
    Vector<std::string> toScan;
    Vector<PluginFormat> swept;

    ScanSessionPlan plan;

    int completedCount = 0;
    int okCount = 0;
    int errorCount = 0;
    int crashedCount = 0;

    bool running = false;
    bool cancelling = false;

    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
};

ScanSession::ScanSession(const FormatManager& formats, ScanSessionOptions optionsToUse)
    : impl(EA::makeOwned<Impl>(*this, formats, std::move(optionsToUse)))
{
}

ScanSession::~ScanSession() = default;

const ScanSessionOptions& ScanSession::options() const
{
    return impl->options;
}

const PluginCache& ScanSession::cache() const
{
    return impl->cache;
}

void ScanSession::start()
{
    impl->start();
}

void ScanSession::poll()
{
    impl->coordinator.poll();
}

void ScanSession::cancel()
{
    impl->cancel();
}

bool ScanSession::isRunning() const
{
    return impl->running;
}

void ScanSession::setAutoPoll(bool shouldAutoPoll)
{
    impl->coordinator.setAutoPoll(shouldAutoPoll);
}

void ScanSession::setPollIntervalMs(int intervalToUse)
{
    impl->coordinator.setPollIntervalMs(intervalToUse);
}

} // namespace MakeASound::Hosting
