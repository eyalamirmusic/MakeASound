#include <MakeASound/DeviceManagerImpl.h>
#include <thread>
#include <random>

float getRandomFloat()
{
    static std::default_random_engine e;
    static std::uniform_real_distribution dis(-0.1f, 0.1f);
    return dis(e);
}

void processBlock(MakeASound::AudioCallbackInfo& info)
{
    if (info.dirty)
    {
        //Allocate memory, etc
    }

    for (size_t channel = 0; channel < info.numOutputs; ++channel)
    {
        auto channelData = info.getOutput<float>(channel);

        for (size_t sample = 0; sample < info.numSamples; ++sample)
            channelData[sample] = getRandomFloat();
    }
}

inline MakeASound::StreamConfig getConfig(MakeASound::DeviceManager& manager,
                                          int block)
{
    auto defaultConfig = manager.getDefaultConfig();
    defaultConfig.maxBlockSize = block;

    return defaultConfig;
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