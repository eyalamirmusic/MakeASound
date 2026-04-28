#include <MakeASound/MakeASound.h>
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

    for (auto channel = 0; channel < info.numOutputs; ++channel)
    {
        auto channelData = info.getOutput<float>(channel);

        for (auto sample = 0; sample < info.numSamples; ++sample)
            channelData[sample] = getRandomFloat();
    }
}


int main()
{
    MakeASound::DeviceManager manager;
    Miro::logJSON(manager.getDefaultConfig());

    return 0;
}