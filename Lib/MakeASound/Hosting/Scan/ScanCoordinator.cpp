#include "ScanCoordinator.h"
#include "Scanner.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Threads/Timer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

namespace MakeASound::Hosting
{

namespace
{
std::int64_t nowMs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();

    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}
} // namespace

int defaultScanJobCount()
{
    const auto cores = static_cast<int>(std::thread::hardware_concurrency());

    return std::clamp(cores, 1, 8);
}

struct ScanCoordinator::Impl
{
    // What an in-process scan hands back to poll(). Shared with the thread, so a
    // Job can move inside the job list while its thread runs.
    struct InProcessState
    {
        std::atomic<bool> finished {false};
        ModuleScan result;
    };

    // One target being scanned: a child process, or a thread.
    struct Job
    {
        std::string target;
        std::int64_t startedAtMs = 0;
        std::shared_ptr<eacp::Processes::Process> process;
        std::shared_ptr<InProcessState> inProcess;
        std::shared_ptr<std::thread> thread;
    };

    Impl(ScanCoordinator& ownerToUse, const FormatManager& formatsToUse)
        : owner(ownerToUse)
        , formats(formatsToUse)
    {
    }

    ~Impl() { stopJobs(); }

    void start(const Vector<std::string>& targets)
    {
        stopRun();

        pending = targets;
        nextPending = 0;
        completedCount = 0;
        totalCount = targets.size();
        running = true;

        if (autoPoll)
            timer = EA::makeOwned<eacp::Threads::Timer>([this] { poll(); },
                                                    eacp::Time::MS {pollIntervalMs});

        if (owner.onProgress)
            owner.onProgress(0, totalCount);

        if (!running)
            return;

        // The first batch starts now rather than on the first tick.
        poll();
    }

    void cancel()
    {
        if (!running)
            return;

        stopRun();

        if (owner.onFinished)
            owner.onFinished();
    }

    void poll()
    {
        if (!running)
            return;

        for (auto i = 0; i < jobs.size();)
        {
            auto outcome = tryFinish(jobs[i]);

            if (!outcome.has_value())
            {
                ++i;
                continue;
            }

            jobs.erase(jobs.begin() + i);
            report(*outcome);

            // report() runs the caller's code, which may cancel or restart.
            if (!running)
                return;
        }

        launchUpToLimit();

        if (jobs.empty() && nextPending >= pending.size())
            finish();
    }

    void report(const ModuleScan& scan)
    {
        ++completedCount;

        if (owner.onModuleScanned)
            owner.onModuleScanned(scan);

        if (running && owner.onProgress)
            owner.onProgress(completedCount, totalCount);
    }

    void finish()
    {
        running = false;
        timer = nullptr;

        if (owner.onFinished)
            owner.onFinished();
    }

    // Ends the run without announcing it: shared by cancel(), which announces it
    // itself, and by a start() that supersedes a run.
    void stopRun()
    {
        running = false;
        timer = nullptr;
        pending = Vector<std::string>();
        nextPending = 0;
        stopJobs();
    }

    void stopJobs()
    {
        for (auto& job: jobs)
        {
            if (job.process != nullptr)
                job.process->kill();

            // An in-process scan is inside plugin code with no way in.
            if (job.thread != nullptr && job.thread->joinable())
                job.thread->join();
        }

        jobs = Vector<Job>();
    }

    void launchUpToLimit()
    {
        while (jobs.size() < maxConcurrent && nextPending < pending.size())
        {
            auto& job = jobs.create();

            job.target = pending[nextPending++];
            job.startedAtMs = nowMs();

            if (mode == ScanMode::InProcess)
                startInProcess(job);
            else
                startSubprocess(job);
        }
    }

    void startSubprocess(Job& job)
    {
        auto options = eacp::Processes::ProcessOptions();

        options.executable = command.executable;
        options.arguments = command.argumentPrefix;
        options.arguments.add(job.target);
        options.captureOutput = true;
        options.noWindow = true;

        job.process = std::make_shared<eacp::Processes::Process>(std::move(options));
    }

    void startInProcess(Job& job)
    {
        auto state = std::make_shared<InProcessState>();
        job.inProcess = state;

        job.thread = std::make_shared<std::thread>(
            [this, state, target = job.target]
            {
                try
                {
                    auto outcome = formats.scanTarget(target);

                    // A request that cannot be acted on is still an answer about
                    // the target: a clean failure, not a crash.
                    if (outcome.request != TargetRequest::ok)
                    {
                        outcome.scan.path = target;
                        outcome.scan.format = formats.formatOfTarget(target);
                        outcome.scan.error = outcome.message;
                    }

                    state->result = std::move(outcome.scan);
                }
                catch (...)
                {
                    state->result = crashedHere(target, "the scan threw");
                }

                state->finished.store(true, std::memory_order_release);
            });
    }

    ModuleScan crashedHere(const std::string& target, std::string error) const
    {
        return crashedScan(target,
                           std::move(error),
                           formats.stampTarget(target),
                           formats.formatOfTarget(target));
    }

    // The result for `job`, or nullopt while it is still working.
    std::optional<ModuleScan> tryFinish(Job& job)
    {
        if (job.inProcess != nullptr)
        {
            if (!job.inProcess->finished.load(std::memory_order_acquire))
                return std::nullopt;

            job.thread->join();
            return job.inProcess->result;
        }

        auto& process = *job.process;

        if (!process.launched())
            return crashedHere(job.target,
                               "the scan worker could not be launched ("
                                   + command.executable + ")");

        if (process.isRunning())
        {
            if (timeout <= 0 || nowMs() - job.startedAtMs < timeout)
                return std::nullopt;

            process.kill();
            process.wait();

            return crashedHere(job.target,
                               "the scan worker was killed after "
                                   + std::to_string(timeout) + " ms");
        }

        // wait() reaps the child and joins the pipe readers, so it comes before
        // output() to see everything the worker wrote.
        const auto exitCode = process.wait();
        const auto errors = stderrTail(process.errorOutput());

        if (exitCode != 0)
            return crashedHere(job.target,
                               "the scan worker exited with code "
                                   + std::to_string(exitCode) + errors);

        auto decoded = decodeScanResult(process.output());

        if (!decoded.has_value())
            return crashedHere(job.target,
                               "the scan worker produced no usable result" + errors);

        // The target as the host spelled it is what the cache keys on.
        decoded->path = job.target;
        return decoded;
    }

    ScanCoordinator& owner;
    const FormatManager& formats;

    WorkerCommand command = defaultWorkerCommand();
    ScanMode mode = ScanMode::Subprocess;
    int maxConcurrent = defaultScanJobCount();
    int timeout = 30000;
    int pollIntervalMs = 25;
    bool autoPoll = false;

    bool running = false;
    Vector<std::string> pending;
    int nextPending = 0;
    int completedCount = 0;
    int totalCount = 0;

    Vector<Job> jobs;
    OwningPointer<eacp::Threads::Timer> timer;
};

ScanCoordinator::ScanCoordinator(const FormatManager& formatsToUse)
    : impl(EA::makeOwned<Impl>(*this, formatsToUse))
{
}

ScanCoordinator::~ScanCoordinator() = default;

void ScanCoordinator::setMode(ScanMode modeToUse)
{
    impl->mode = modeToUse;
}

ScanMode ScanCoordinator::mode() const
{
    return impl->mode;
}

void ScanCoordinator::setWorkerCommand(WorkerCommand commandToUse)
{
    impl->command = std::move(commandToUse);
}

const WorkerCommand& ScanCoordinator::workerCommand() const
{
    return impl->command;
}

void ScanCoordinator::setMaxConcurrent(int maxConcurrentToUse)
{
    impl->maxConcurrent = std::max(maxConcurrentToUse, 1);
}

int ScanCoordinator::maxConcurrent() const
{
    return impl->maxConcurrent;
}

void ScanCoordinator::setTimeoutMs(int timeoutToUse)
{
    impl->timeout = timeoutToUse;
}

int ScanCoordinator::timeoutMs() const
{
    return impl->timeout;
}

void ScanCoordinator::setAutoPoll(bool shouldAutoPoll)
{
    impl->autoPoll = shouldAutoPoll;
}

void ScanCoordinator::setPollIntervalMs(int intervalToUse)
{
    impl->pollIntervalMs = std::max(intervalToUse, 1);
}

void ScanCoordinator::poll()
{
    impl->poll();
}

void ScanCoordinator::start(const Vector<std::string>& targets)
{
    impl->start(targets);
}

void ScanCoordinator::cancel()
{
    impl->cancel();
}

bool ScanCoordinator::isRunning() const
{
    return impl->running;
}

int ScanCoordinator::completed() const
{
    return impl->completedCount;
}

int ScanCoordinator::total() const
{
    return impl->totalCount;
}

} // namespace MakeASound::Hosting
