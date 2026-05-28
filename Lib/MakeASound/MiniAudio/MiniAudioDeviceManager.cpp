#include "MiniAudioDeviceManager.h"

#include <stdexcept>
#include <string>

namespace MakeASound::MiniAudio
{

namespace
{
ma_device_config makeDeviceConfig(const StreamConfig& streamConfig,
                                  const ma_device_id* playbackId,
                                  const ma_device_id* captureId)
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

    if (wantsPlayback)
    {
        config.playback.pDeviceID = playbackId;
        config.playback.format = ma_format_f32;
        config.playback.channels =
            static_cast<ma_uint32>(streamConfig.output->nChannels);
    }

    if (wantsCapture)
    {
        config.capture.pDeviceID = captureId;
        config.capture.format = ma_format_f32;
        config.capture.channels =
            static_cast<ma_uint32>(streamConfig.input->nChannels);
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

void deinterleave(const float* src, float* dst, int channels, int frames)
{
    for (auto frame = 0; frame < frames; ++frame)
        for (auto ch = 0; ch < channels; ++ch)
            dst[ch * frames + frame] = src[frame * channels + ch];
}

void interleave(const float* src, float* dst, int channels, int frames)
{
    for (auto frame = 0; frame < frames; ++frame)
        for (auto ch = 0; ch < channels; ++ch)
            dst[frame * channels + ch] = src[ch * frames + frame];
}
} // namespace

DeviceManager::DeviceManager()
{
    auto result = ma_context_init(nullptr, 0, nullptr, &context);

    if (result != MA_SUCCESS)
        throw std::runtime_error(std::string("miniaudio: ma_context_init failed: ")
                                 + ma_result_description(result));

    contextInitialised = true;
}

DeviceManager::~DeviceManager()
{
    if (deviceInitialised)
    {
        ma_device_uninit(&device);
        deviceInitialised = false;
    }

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
    info.currentSampleRate = info.preferredSampleRate;

    if (type == ma_device_type_playback)
        info.isDefaultOutput = enumInfo.isDefault != 0;
    else
        info.isDefaultInput = enumInfo.isDefault != 0;

    return info;
}

void DeviceManager::refreshDeviceCache()
{
    deviceCache.clear();

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
        throw std::runtime_error(
            std::string("miniaudio: ma_context_get_devices failed: ")
            + ma_result_description(result));

    auto nextId = 0;

    for (auto i = 0u; i < playbackCount; ++i)
    {
        auto entry = CachedDevice {};
        entry.id = nextId++;
        entry.playbackId = playbackInfos[i].id;
        entry.hasPlayback = true;
        entry.info =
            buildDeviceInfo(playbackInfos[i], ma_device_type_playback, entry.id);
        deviceCache.push_back(std::move(entry));
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
                    if (std::ranges::find(cached.info.sampleRates, rate)
                        == cached.info.sampleRates.end())
                        cached.info.sampleRates.push_back(rate);

                std::ranges::sort(cached.info.sampleRates);
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
            deviceCache.push_back(std::move(entry));
        }
    }
}

Vector<DeviceInfo> DeviceManager::getDevices()
{
    refreshDeviceCache();

    auto result = Vector<DeviceInfo> {};
    result.reserve(static_cast<int>(deviceCache.size()));

    for (const auto& cached: deviceCache)
        result.add(cached.info);

    return result;
}

DeviceInfo DeviceManager::getDefaultInputDevice()
{
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

void DeviceManager::start()
{
    if (!deviceInitialised)
        return;

    auto result = ma_device_start(&device);

    if (result != MA_SUCCESS)
        throw std::runtime_error(std::string("miniaudio: ma_device_start failed: ")
                                 + ma_result_description(result));
}

void DeviceManager::stop()
{
    if (!deviceInitialised)
        return;

    if (ma_device_is_started(&device))
        ma_device_stop(&device);

    ma_device_uninit(&device);
    deviceInitialised = false;
}

int DeviceManager::openStream(const StreamConfig& configToUse)
{
    config = configToUse;

    if (deviceCache.empty())
        refreshDeviceCache();

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

    auto deviceConfig = makeDeviceConfig(config, playbackId, captureId);
    deviceConfig.dataCallback = audioCallback;
    deviceConfig.pUserData = this;

    auto result = ma_device_init(&context, &deviceConfig, &device);

    if (result != MA_SUCCESS)
        throw std::runtime_error(std::string("miniaudio: ma_device_init failed: ")
                                 + ma_result_description(result));

    deviceInitialised = true;
    framesElapsed = 0;

    config.maxBlockSize = static_cast<int>(
        std::max(device.playback.internalPeriodSizeInFrames,
                 device.capture.internalPeriodSizeInFrames));

    if (config.maxBlockSize == 0)
        config.maxBlockSize = static_cast<int>(deviceConfig.periodSizeInFrames);

    auto inChannels = config.getInputChannels();
    auto outChannels = config.getOutputChannels();

    inputScratch.assign(
        static_cast<size_t>(inChannels) * static_cast<size_t>(config.maxBlockSize),
        0.0f);
    outputScratch.assign(
        static_cast<size_t>(outChannels) * static_cast<size_t>(config.maxBlockSize),
        0.0f);

    return config.maxBlockSize;
}

long DeviceManager::getStreamLatency()
{
    if (!deviceInitialised)
        return 0;

    auto playbackLatency = static_cast<long>(device.playback.internalPeriodSizeInFrames
                                             * device.playback.internalPeriods);
    auto captureLatency = static_cast<long>(device.capture.internalPeriodSizeInFrames
                                            * device.capture.internalPeriods);

    return std::max(playbackLatency, captureLatency);
}

int DeviceManager::getStreamSampleRate()
{
    if (!deviceInitialised)
        return 0;

    return static_cast<int>(device.sampleRate);
}

void DeviceManager::onCallback(void* output, const void* input, ma_uint32 frameCount)
{
    if (!callback)
        return;

    auto frames = static_cast<int>(frameCount);
    auto inChannels = config.getInputChannels();
    auto outChannels = config.getOutputChannels();

    auto neededInput =
        static_cast<size_t>(inChannels) * static_cast<size_t>(frames);
    auto neededOutput =
        static_cast<size_t>(outChannels) * static_cast<size_t>(frames);

    if (inputScratch.size() < neededInput)
        inputScratch.assign(neededInput, 0.0f);

    if (outputScratch.size() < neededOutput)
        outputScratch.assign(neededOutput, 0.0f);

    if (inChannels > 0 && input != nullptr)
        deinterleave(static_cast<const float*>(input),
                     inputScratch.data(),
                     inChannels,
                     frames);

    if (outChannels > 0)
        std::fill(outputScratch.begin(),
                  outputScratch.begin() + static_cast<std::ptrdiff_t>(neededOutput),
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

    callback(info);

    if (outChannels > 0 && output != nullptr)
        interleave(outputScratch.data(),
                   static_cast<float*>(output),
                   outChannels,
                   frames);

    framesElapsed += frameCount;
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

} // namespace MakeASound::MiniAudio
