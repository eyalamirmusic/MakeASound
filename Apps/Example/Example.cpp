#include <MakeASound/MakeASound.h>
#include <thread>
#include <random>

namespace MS = MakeASound;

float getRandomFloat()
{
    static std::default_random_engine e;
    static std::uniform_real_distribution dis(-0.1f, 0.1f);
    return dis(e);
}

void processBlock(MS::AudioCallbackInfo& info)
{
    if (info.dirty)
    {
        //Allocate memory, etc
    }

    for (auto channel = 0; channel < info.numOutputs; ++channel)
    {
        auto channelData = info.getOutput(channel);

        for (auto& sample: channelData)
            sample = getRandomFloat();
    }
}


int main()
{
    MS::DeviceManager manager;
    Miro::logJSON(manager.getDefaultConfig());

    return 0;
}