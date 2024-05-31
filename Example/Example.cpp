#include <MakeASound/MakeASound.h>
#include <thread>
#include <random>

float getRandomFloat()
{
    static std::default_random_engine e;
    static std::uniform_real_distribution dis(-0.1f, 0.1f);
    return dis(e);
}

MakeASound::AudioCallbackInfo lastInfo;

void processBlock(MakeASound::AudioCallbackInfo& info)
{
    if (info != lastInfo)
    {
        lastInfo = info;
        std::cout << "Block Size: " << info.maxBlockSize << std::endl;
    }

    for (size_t channel = 0; channel < info.numOutputs; ++channel)
    {
        auto channelData = info.getOutput<float>(channel);

        for (size_t sample = 0; sample < info.numSamples; ++sample)
            channelData[sample] = getRandomFloat();
    }
}

MakeASound::StreamConfig getConfig(MakeASound::DeviceManager& manager, int blockSize)
{
    MakeASound::StreamConfig config;

    auto devices = manager.getDevices();
    config.output = MakeASound::StreamParameters(devices[1], false);

    config.sampleRate = 44100;
    config.maxBlockSize = blockSize;

    return config;
}

int main()
{
    MakeASound::DeviceManager manager;

    manager.start(getConfig(manager, 512), processBlock);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    manager.start(getConfig(manager, 256), processBlock);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    return 0;
}