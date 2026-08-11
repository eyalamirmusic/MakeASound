#include <MakeASound/MakeASound.h>

#include <chrono>
#include <iostream>
#include <random>
#include <string_view>
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

void listDrivers(const MS::DeviceManager& manager)
{
    std::cout << "Drivers:\n";

    for (auto backend: manager.getAvailableBackends())
    {
        auto current = backend == manager.getBackend() ? "  (current)" : "";
        std::cout << "  " << MS::getBackendName(backend) << current << '\n';
    }
}

bool selectDriver(MS::DeviceManager& manager, std::string_view name)
{
    auto backend = MS::getBackendFromName(name);

    if (!backend.has_value())
    {
        std::cout << "Unknown driver: " << name << '\n';
        return false;
    }

    if (auto error = manager.setBackend(*backend); error != MS::Error::NoError)
    {
        std::cout << MS::getBackendName(*backend)
                  << " is not available here: " << MS::getErrorMessage(error)
                  << '\n';
        return false;
    }

    return true;
}
} // namespace

int main(int argc, char** argv)
{
    auto manager = MS::DeviceManager {};

    listDrivers(manager);

    if (argc > 1 && !selectDriver(manager, argv[1]))
        return 1;

    std::cout << "Using " << MS::getBackendName(manager.getBackend()) << '\n';

    auto config = manager.getDefaultConfig();

    Miro::logJSON(config);

    if (auto error = manager.start(config, renderNoise); error != MS::Error::NoError)
    {
        std::cout << MS::getErrorMessage(error) << '\n';
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
}
