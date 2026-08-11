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

// Switch the manager onto the driver named on the command line. Anything the machine
// doesn't answer to is worth saying out loud rather than silently running on the
// default — the point of passing a name is to hear that one specifically.
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

    // One optional argument: the driver to stream through, by name. Without it the
    // machine's own default is used, which is what most callers want.
    if (argc > 1 && !selectDriver(manager, argv[1]))
        return 1;

    std::cout << "Using " << MS::getBackendName(manager.getBackend()) << '\n';

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
