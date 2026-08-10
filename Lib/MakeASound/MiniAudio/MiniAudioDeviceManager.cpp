#include "MiniAudioDeviceManager.h"
#include "../Devices/DeviceQueries.h"

#include <algorithm>
#include <chrono>

namespace MakeASound::MiniAudio
{

namespace
{
// How long recovery waits between attempts at re-opening a device that isn't back yet.
// Short enough that a sample-rate change is a glitch rather than a dropout, long enough
// that a device which is gone for good costs almost nothing to keep waiting for.
constexpr auto kRecoveryRetryInterval = std::chrono::milliseconds(250);

// How often the watchdog looks, and how long a stream that is supposed to be running may
// go without a data callback before it counts as dead. A block is a few milliseconds, so
// a whole second of nothing is not a scheduling hiccup — but it is short enough that the
// gap reads as a glitch rather than an outage.
constexpr auto kWatchdogInterval = std::chrono::milliseconds(250);
constexpr auto kStarvationTimeoutMs = std::int64_t {1000};

std::int64_t nowMs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

ma_device_config makeDeviceConfig(const StreamConfig& streamConfig,
                                  const ma_device_id* playbackId,
                                  const ma_device_id* captureId,
                                  int nativePlaybackChannels,
                                  int nativeCaptureChannels)
{
    auto wantsPlayback = streamConfig.output.has_value();
    auto wantsCapture = streamConfig.input.has_value();

    auto type = ma_device_type_playback;

    if (wantsPlayback && wantsCapture)
        type = ma_device_type_duplex;
    else if (wantsCapture)
        type = ma_device_type_capture;

    auto config = ma_device_config_init(type);

    config.sampleRate = static_cast<ma_uint32>(streamConfig.sampleRate);
    config.periodSizeInFrames = static_cast<ma_uint32>(streamConfig.maxBlockSize);

    // Open the device with its full native channel count (not just the selected
    // slice) so miniaudio hands us the raw physical channels with no channel
    // conversion / down-mixing. The callback picks out the selected channels.
    if (wantsPlayback)
    {
        config.playback.pDeviceID = playbackId;
        config.playback.format = ma_format_f32;
        config.playback.channels =
            static_cast<ma_uint32>(nativePlaybackChannels);
    }

    if (wantsCapture)
    {
        config.capture.pDeviceID = captureId;
        config.capture.format = ma_format_f32;
        config.capture.channels = static_cast<ma_uint32>(nativeCaptureChannels);
    }

    if (streamConfig.options.has_value())
    {
        const auto& options = streamConfig.options.value();

        if (options.numberOfBuffers > 0)
            config.periods = static_cast<ma_uint32>(options.numberOfBuffers);

        if (options.flags.minimizeLatency)
        {
            config.periods = 2;
            config.performanceProfile = ma_performance_profile_low_latency;
        }

        if (options.flags.hogDevice)
        {
            config.playback.shareMode = ma_share_mode_exclusive;
            config.capture.shareMode = ma_share_mode_exclusive;
        }
    }

    return config;
}

// De-interleave a contiguous slice of channels out of a wider interleaved
// buffer. src has srcChannels per frame; we take [firstChannel, firstChannel +
// count) and lay them out planar in dst.
void deinterleaveSlice(const float* src,
                       float* dst,
                       int srcChannels,
                       int firstChannel,
                       int count,
                       int frames)
{
    for (auto frame = 0; frame < frames; ++frame)
        for (auto ch = 0; ch < count; ++ch)
            dst[ch * frames + frame] =
                src[frame * srcChannels + (firstChannel + ch)];
}

// Interleave planar channels into a slice of a wider interleaved buffer. dst
// has dstChannels per frame; the planar src is written to [firstChannel,
// firstChannel + count). Channels outside the slice are left untouched.
void interleaveSlice(const float* src,
                     float* dst,
                     int dstChannels,
                     int firstChannel,
                     int count,
                     int frames)
{
    for (auto frame = 0; frame < frames; ++frame)
        for (auto ch = 0; ch < count; ++ch)
            dst[frame * dstChannels + (firstChannel + ch)] =
                src[ch * frames + frame];
}
} // namespace

DeviceManager::DeviceManager()
{
    auto result = ma_context_init(nullptr, 0, nullptr, &context);

    // A backend that won't initialise leaves the manager alive but empty: it enumerates
    // nothing and refuses to open a stream, both of which the host can report. Failing
    // the constructor instead would take down an application over a machine that simply
    // has no working audio backend.
    if (result != MA_SUCCESS)
    {
        setError(getError(result));
        return;
    }

    contextInitialised = true;
}

DeviceManager::~DeviceManager()
{
    // Worker first: it re-opens the very device and context torn down below, so it has
    // to be off the field before either goes.
    {
        auto lock = std::lock_guard(recoveryMutex);
        recoveryQuit = true;
    }

    recoveryCv.notify_all();

    if (recoveryThread.joinable())
        recoveryThread.join();

    stop();

    if (contextInitialised)
    {
        ma_context_uninit(&context);
        contextInitialised = false;
    }
}

DeviceInfo DeviceManager::buildDeviceInfo(const ma_device_info& enumInfo,
                                          ma_device_type type,
                                          int assignedId)
{
    auto detailed = ma_device_info {};
    auto result =
        ma_context_get_device_info(&context, type, &enumInfo.id, &detailed);

    auto& source = (result == MA_SUCCESS) ? detailed : enumInfo;

    auto info = DeviceInfo {};
    info.id = assignedId;
    info.name = source.name;

    auto channels = 0;
    for (auto i = 0u; i < source.nativeDataFormatCount; ++i)
        channels = std::max(channels,
                            static_cast<int>(source.nativeDataFormats[i].channels));

    if (type == ma_device_type_playback)
        info.outputChannels = channels;
    else
        info.inputChannels = channels;

    info.sampleRates = collectSampleRates(source);
    info.preferredSampleRate = pickPreferredSampleRate(info.sampleRates);

    // What the device is actually on, asked of the platform — miniaudio's device info
    // only lists what is *supported*. The preferred rate is the fallback, not the
    // answer: the two differ the moment any app moves a shared device.
    auto current = getCurrentSampleRate(info);
    info.currentSampleRate = current > 0 ? current : info.preferredSampleRate;

    if (type == ma_device_type_playback)
        info.isDefaultOutput = enumInfo.isDefault != 0;
    else
        info.isDefaultInput = enumInfo.isDefault != 0;

    return info;
}

Error DeviceManager::setError(Error error)
{
    lastError = error;
    return error;
}

Error DeviceManager::refreshDeviceCache()
{
    deviceCache.clear();

    // An empty cache is the honest answer when there is nothing to enumerate with —
    // callers read it as "no devices", which is exactly the situation.
    if (!contextInitialised)
        return setError(Error::SYSTEM_ERROR);

    ma_device_info* playbackInfos = nullptr;
    auto playbackCount = ma_uint32 {0};
    ma_device_info* captureInfos = nullptr;
    auto captureCount = ma_uint32 {0};

    auto result = ma_context_get_devices(&context,
                                         &playbackInfos,
                                         &playbackCount,
                                         &captureInfos,
                                         &captureCount);

    if (result != MA_SUCCESS)
        return setError(getError(result));

    auto nextId = 0;

    for (auto i = 0u; i < playbackCount; ++i)
    {
        auto entry = CachedDevice {};
        entry.id = nextId++;
        entry.playbackId = playbackInfos[i].id;
        entry.hasPlayback = true;
        entry.info =
            buildDeviceInfo(playbackInfos[i], ma_device_type_playback, entry.id);
        deviceCache.add(std::move(entry));
    }

    for (auto i = 0u; i < captureCount; ++i)
    {
        auto matched = false;

        for (auto& cached: deviceCache)
        {
            if (!cached.hasPlayback)
                continue;

            if (cached.info.name == captureInfos[i].name)
            {
                cached.captureId = captureInfos[i].id;
                cached.hasCapture = true;

                auto captureInfo =
                    buildDeviceInfo(captureInfos[i], ma_device_type_capture, cached.id);

                cached.info.inputChannels = captureInfo.inputChannels;
                cached.info.isDefaultInput = captureInfo.isDefaultInput;
                cached.info.duplexChannels = std::min(cached.info.outputChannels,
                                                     cached.info.inputChannels);

                for (auto rate: captureInfo.sampleRates)
                    cached.info.sampleRates.addIfNotThere(rate);

                cached.info.sampleRates.sort();
                matched = true;
                break;
            }
        }

        if (!matched)
        {
            auto entry = CachedDevice {};
            entry.id = nextId++;
            entry.captureId = captureInfos[i].id;
            entry.hasCapture = true;
            entry.info =
                buildDeviceInfo(captureInfos[i], ma_device_type_capture, entry.id);
            deviceCache.add(std::move(entry));
        }
    }

    return Error::NoError;
}

Vector<DeviceInfo> DeviceManager::getDevices()
{
    // Enumerating rebuilds the cache, which recovery reads while re-opening.
    auto lock = std::lock_guard(deviceMutex);
    refreshDeviceCache();

    auto result = Vector<DeviceInfo> {};
    result.reserve(deviceCache.size());

    for (const auto& cached: deviceCache)
        result.add(cached.info);

    return result;
}

DeviceInfo DeviceManager::getDefaultInputDevice()
{
    auto lock = std::lock_guard(deviceMutex);
    refreshDeviceCache();

    for (const auto& cached: deviceCache)
        if (cached.hasCapture && cached.info.isDefaultInput)
            return cached.info;

    for (const auto& cached: deviceCache)
        if (cached.hasCapture)
            return cached.info;

    return {};
}

DeviceInfo DeviceManager::getDefaultOutputDevice()
{
    auto lock = std::lock_guard(deviceMutex);
    refreshDeviceCache();

    for (const auto& cached: deviceCache)
        if (cached.hasPlayback && cached.info.isDefaultOutput)
            return cached.info;

    for (const auto& cached: deviceCache)
        if (cached.hasPlayback)
            return cached.info;

    return {};
}

const ma_device_id* DeviceManager::findDeviceId(int makeASoundId) const
{
    for (const auto& cached: deviceCache)
        if (cached.id == makeASoundId)
            return cached.hasPlayback ? &cached.playbackId : &cached.captureId;

    return nullptr;
}

Error DeviceManager::start(const StreamConfig& configToUse)
{
    auto lock = std::lock_guard(deviceMutex);

    config = configToUse;

    // Set from the host's intent, not from whether the open worked. A stream that
    // couldn't find its device is still one that is meant to be running, and this is
    // what tells the recovery worker to keep trying.
    shouldRun = true;
    ensureRecoveryThread();

    auto error = openStreamLocked();

    if (error == Error::NoError)
        error = startLocked();

    // Worth another go only if there is a device named to go back to. A config with
    // neither side set describes nothing, so retrying it would burn an enumeration
    // every quarter second to arrive at the same answer.
    auto namesADevice = config.input.has_value() || config.output.has_value();

    if (error != Error::NoError && autoRecover && namesADevice)
        requestRecovery();

    return error;
}

Error DeviceManager::startLocked()
{
    if (!deviceInitialised)
        return setError(Error::INVALID_DEVICE);

    // Cleared before the device can call back, so the watchdog measures this stream's
    // silence and not the gap left by the one it replaced.
    lastCallbackMs = 0;

    auto result = ma_device_start(&device);

    if (result != MA_SUCCESS)
        return setError(getError(result));

    streamRunning = true;
    return setError(Error::NoError);
}

bool DeviceManager::isRunning() const
{
    return streamRunning.load();
}

Error DeviceManager::getLastError() const
{
    return lastError.load();
}

void DeviceManager::stop()
{
    auto lock = std::lock_guard(deviceMutex);

    // Before the teardown, so a stopped notification racing us finds recovery already
    // switched off rather than re-opening the stream the host just closed.
    shouldRun = false;
    stopLocked();
}

void DeviceManager::stopLocked()
{
    streamRunning = false;

    if (!deviceInitialised)
        return;

    // Both calls below make the OS report the device as stopped, and that report
    // arrives through the same path the OS uses when it stops the device on its own.
    // Swallow ours so `Stopped` only ever means the device went away.
    stopping = true;

    if (ma_device_is_started(&device))
        ma_device_stop(&device);

    ma_device_uninit(&device);
    deviceInitialised = false;
    stopping = false;
}

Error DeviceManager::openStreamLocked()
{
    if (!contextInitialised)
        return setError(Error::SYSTEM_ERROR);

    if (deviceCache.empty())
        refreshDeviceCache();

    // Nothing to open. Answered here rather than left to ma_device_init so a host that
    // asks what went wrong is told there are no devices, instead of whatever the
    // backend makes of a stream with no sides to it.
    if (!config.input.has_value() && !config.output.has_value())
        return setError(Error::NO_DEVICES_FOUND);

    const ma_device_id* playbackId = nullptr;
    const ma_device_id* captureId = nullptr;

    if (config.output.has_value())
    {
        for (const auto& cached: deviceCache)
        {
            if (cached.id == config.output->device.id && cached.hasPlayback)
            {
                playbackId = &cached.playbackId;
                break;
            }
        }
    }

    if (config.input.has_value())
    {
        for (const auto& cached: deviceCache)
        {
            if (cached.id == config.input->device.id && cached.hasCapture)
            {
                captureId = &cached.captureId;
                break;
            }
        }
    }

    auto nativePlayback =
        config.output.has_value() ? config.output->device.outputChannels : 0;
    auto nativeCapture =
        config.input.has_value() ? config.input->device.inputChannels : 0;

    auto deviceConfig = makeDeviceConfig(config,
                                         playbackId,
                                         captureId,
                                         nativePlayback,
                                         nativeCapture);
    deviceConfig.dataCallback = audioCallback;
    deviceConfig.notificationCallback = deviceNotificationCallback;
    deviceConfig.pUserData = this;

    auto result = ma_device_init(&context, &deviceConfig, &device);

    if (result != MA_SUCCESS)
        return setError(getError(result));

    deviceInitialised = true;
    framesElapsed = 0;

    config.maxBlockSize = static_cast<int>(
        std::max(device.playback.internalPeriodSizeInFrames,
                 device.capture.internalPeriodSizeInFrames));

    if (config.maxBlockSize == 0)
        config.maxBlockSize = static_cast<int>(deviceConfig.periodSizeInFrames);

    // The channel counts miniaudio actually negotiated are what the callback's
    // interleaved buffers carry; clamp the requested slice to what's available.
    captureChannels = static_cast<int>(device.capture.channels);
    playbackChannels = static_cast<int>(device.playback.channels);

    auto clampSlice = [](int available, int first, int count, int& outFirst, int& outCount)
    {
        outCount = std::clamp(count, 0, available);
        outFirst = std::clamp(first, 0, std::max(0, available - outCount));
    };

    clampSlice(captureChannels,
               config.input.has_value() ? config.input->firstChannel : 0,
               config.getInputChannels(),
               inputFirstChannel,
               inputChannelCount);

    clampSlice(playbackChannels,
               config.output.has_value() ? config.output->firstChannel : 0,
               config.getOutputChannels(),
               outputFirstChannel,
               outputChannelCount);

    inputScratch.assign(inputChannelCount * config.maxBlockSize, 0.0f);
    outputScratch.assign(outputChannelCount * config.maxBlockSize, 0.0f);

    return setError(Error::NoError);
}

long DeviceManager::getStreamLatency() const
{
    if (!deviceInitialised)
        return 0;

    auto playbackLatency = static_cast<long>(device.playback.internalPeriodSizeInFrames)
                           * static_cast<long>(device.playback.internalPeriods);
    auto captureLatency = static_cast<long>(device.capture.internalPeriodSizeInFrames)
                          * static_cast<long>(device.capture.internalPeriods);

    return std::max(playbackLatency, captureLatency);
}

int DeviceManager::getStreamSampleRate() const
{
    if (!deviceInitialised)
        return 0;

    return static_cast<int>(device.sampleRate);
}

void DeviceManager::onCallback(void* output, const void* input, ma_uint32 frameCount)
{
    // Before the early-out: this is the watchdog's proof of life, and a stream whose
    // host has no callback set is still a live stream.
    lastCallbackMs = nowMs();

    if (!callback)
        return;

    auto frames = static_cast<int>(frameCount);
    auto inChannels = inputChannelCount;
    auto outChannels = outputChannelCount;

    auto neededInput = inChannels * frames;
    auto neededOutput = outChannels * frames;

    if (static_cast<int>(inputScratch.size()) < neededInput)
        inputScratch.assign(neededInput, 0.0f);

    if (static_cast<int>(outputScratch.size()) < neededOutput)
        outputScratch.assign(neededOutput, 0.0f);

    if (inChannels > 0 && input != nullptr)
        deinterleaveSlice(static_cast<const float*>(input),
                          inputScratch.data(),
                          captureChannels,
                          inputFirstChannel,
                          inChannels,
                          frames);

    if (outChannels > 0)
        std::fill(outputScratch.begin(),
                  outputScratch.begin() + neededOutput,
                  0.0f);

    auto info = AudioCallbackInfo {};
    info.inputBuffer = inputScratch.data();
    info.outputBuffer = outputScratch.data();
    info.numSamples = frames;
    info.numInputs = inChannels;
    info.numOutputs = outChannels;
    info.sampleRate = static_cast<int>(device.sampleRate);
    info.maxBlockSize = config.maxBlockSize;
    info.latency = static_cast<int>(getStreamLatency());
    info.streamTime =
        static_cast<double>(framesElapsed) / static_cast<double>(device.sampleRate);
    info.status = AudioCallbackStatus::OK;

    // Audio is flowing again after the device changed under us — tell this callback
    // even if nobody registered for notifications. The façade only ever raises dirty,
    // so its own shape check still applies on top of this.
    if (notificationPending.exchange(false))
        info.dirty = true;

    callback(info);

    // The device owns every native output channel, but we only fill the
    // selected slice — clear the whole buffer so unselected channels stay
    // silent, then write our slice into place.
    if (playbackChannels > 0 && output != nullptr)
    {
        auto* out = static_cast<float*>(output);
        std::fill(out, out + playbackChannels * frames, 0.0f);

        if (outChannels > 0)
            interleaveSlice(outputScratch.data(),
                            out,
                            playbackChannels,
                            outputFirstChannel,
                            outChannels,
                            frames);
    }

    framesElapsed += frameCount;
}

void DeviceManager::notifyHost(DeviceNotification notification)
{
    // Set even with no callback registered: the next audio callback reads it and
    // raises `dirty`, which is all a host that only implements the audio callback
    // needs to know its cached view of the stream is stale.
    notificationPending = true;

    if (notificationCallback)
        notificationCallback(notification);
}

void DeviceManager::onNotification(ma_device_notification_type type)
{
    if (stopping)
        return;

    notifyHost(getNotification(type));

    // Only a stop needs recovering from. Handing `started` to the worker would have it
    // tear down the stream it was just told came up.
    if (type == ma_device_notification_type_stopped && autoRecover)
        requestRecovery();
}

bool DeviceManager::isStarved() const
{
    if (!shouldRun || !autoRecover)
        return false;

    auto last = lastCallbackMs.load();

    // 0 means no callback has run since the stream opened. Waiting for the first one
    // rather than treating it as starvation keeps a device that is still spinning up
    // from being torn down underneath itself.
    return last > 0 && nowMs() - last > kStarvationTimeoutMs;
}

void DeviceManager::ensureRecoveryThread()
{
    if (recoveryThread.joinable())
        return;

    recoveryThread = std::thread([this] { runRecovery(); });
}

void DeviceManager::requestRecovery()
{
    {
        auto lock = std::lock_guard(recoveryMutex);
        recoveryRequested = true;
    }

    recoveryCv.notify_one();
}

void DeviceManager::runRecovery()
{
    auto lock = std::unique_lock(recoveryMutex);

    while (true)
    {
        // Timed rather than indefinite. A notification is the fast path, but it is not a
        // guarantee: a driver can stop delivering audio while the OS goes on reporting
        // the device as running, and waiting only to be told would sit here forever with
        // a stream that is already dead.
        recoveryCv.wait_for(lock,
                            kWatchdogInterval,
                            [this] { return recoveryRequested || recoveryQuit; });

        if (recoveryQuit)
            return;

        auto starved = !recoveryRequested && isStarved();

        if (!recoveryRequested && !starved)
            continue;

        recoveryRequested = false;

        if (starved)
        {
            // Nobody told the host the device died, because nobody told us either. Say
            // it now, so a host watching notifications sees the same event it would
            // have seen had the OS been forthcoming.
            lock.unlock();
            notifyHost(DeviceNotification::Stopped);
            lock.lock();
        }

        // Keep trying rather than giving up after one go: the device is often not ready
        // to be re-opened the instant it dies (the sample rate change that killed it is
        // still settling), and one that was unplugged comes back when it comes back.
        while (!recoveryQuit)
        {
            lock.unlock();
            auto recovered = tryReopen();
            lock.lock();

            if (recovered)
                break;

            // Interruptible, so a device that is genuinely gone costs one wake-up per
            // interval and teardown never waits on it.
            recoveryCv.wait_for(lock,
                                kRecoveryRetryInterval,
                                [this] { return recoveryQuit; });
        }
    }
}

bool DeviceManager::tryReopen()
{
    auto lock = std::lock_guard(deviceMutex);

    // The host stopped the stream while we were getting here — that decision wins.
    if (!shouldRun)
        return true;

    stopLocked();
    repointConfigToCache();

    // Anything the backend refuses (device absent, rate not yet settled) is worth
    // another attempt; the caller paces them.
    if (openStreamLocked() != Error::NoError)
        return false;

    return startLocked() == Error::NoError;
}

void DeviceManager::repointConfigToCache()
{
    refreshDeviceCache();

    auto repoint = [this](std::optional<StreamParameters>& params, bool input)
    {
        if (!params.has_value())
            return;

        for (const auto& cached: deviceCache)
        {
            if (cached.info.name != params->device.name)
                continue;

            if (input ? cached.hasCapture : cached.hasPlayback)
            {
                // Whole info, not just the id: channel counts and rates are what the
                // re-open negotiates against, and the device may come back different.
                params->device = cached.info;
                return;
            }
        }

        // Nothing carries that name any more. Left as it is, openStream finds no
        // matching cache entry, passes a null device id, and miniaudio opens the
        // system default — audio keeps flowing from whatever the machine has.
    };

    repoint(config.input, true);
    repoint(config.output, false);
}

void audioCallback(ma_device* dev,
                   void* output,
                   const void* input,
                   ma_uint32 frameCount)
{
    auto* manager = static_cast<DeviceManager*>(dev->pUserData);

    if (manager != nullptr)
        manager->onCallback(output, input, frameCount);
}

void deviceNotificationCallback(const ma_device_notification* notification)
{
    if (notification == nullptr || notification->pDevice == nullptr)
        return;

    auto* manager = static_cast<DeviceManager*>(notification->pDevice->pUserData);

    if (manager != nullptr)
        manager->onNotification(notification->type);
}

} // namespace MakeASound::MiniAudio
