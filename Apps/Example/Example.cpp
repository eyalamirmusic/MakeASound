#include <MakeASound/MakeASound.h>

#include <chrono>
#include <random>
#include <thread>

namespace MS = MakeASound;

namespace
{
float nextNoiseSample()
{
    static auto engine = std::default_random_engine {std::random_device {}()};
    static auto dist = std::uniform_real_distribution<float> {-0.1f, 0.1f};
    return dist(engine);
}

void renderNoise(MS::AudioCallbackInfo& info)
{
    for (auto channel = 0; channel < info.numOutputs; ++channel)
        for (auto& sample: info.getOutput(channel))
            sample = nextNoiseSample();
}
} // namespace

int main()
{
    auto manager = MS::DeviceManager {};
    auto config = manager.getDefaultConfig();

    Miro::logJSON(config);

    manager.start(config, renderNoise);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
}
