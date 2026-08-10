#include <MakeASound/MakeASound.h>

#include <chrono>
#include <iostream>
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
    for (auto channel: info.getOutput().channels())
        for (auto& sample: channel)
            sample = nextNoiseSample();
}
} // namespace

int main()
{
    auto manager = MS::DeviceManager {};
    auto config = manager.getDefaultConfig();

    Miro::logJSON(config);

    // A machine with no audio device is a normal thing to run into, so say so and
    // leave rather than sitting through two seconds of silence.
    if (auto error = manager.start(config, renderNoise); error != MS::Error::NoError)
    {
        std::cout << MS::getErrorMessage(error) << '\n';
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
}
