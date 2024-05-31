#include <MakeASound/MakeASound.h>
#include <thread>
#include <random>

float getRandomFloat()
{
    static std::default_random_engine e;
    static std::uniform_real_distribution dis(-0.1f, 0.1f);
    return dis(e);
}

void audioCallback(MakeASound::AudioCallbackInfo& info)
{
    for (size_t channel = 0; channel < info.numOutputs; ++channel)
    {
        auto channelData = info.getOutput<float>(channel);

        for (size_t sample = 0; sample < info.nFrames; ++sample)
            channelData[sample] = getRandomFloat();
    }
}

int main()
{
    MakeASound::DeviceManager manager;

    auto config = manager.getDefaultConfig();
    config.callback = audioCallback;

    manager.openStream(config);
    manager.startStream();

    while (true)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    return 0;
}