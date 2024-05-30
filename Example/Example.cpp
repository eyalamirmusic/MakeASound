#include <MakeASound/MakeASound.h>
#include <iostream>
#include <thread>


int main()
{
    MakeASound::DeviceManager manager;

    auto config = manager.getDefaultConfig();

    config.callback = [](MakeASound::AudioCallbackInfo& info)
    {
        std::cout << info.nFrames;
    };

    manager.openStream(config);
    manager.startStream();

    while (true)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    return 0;
}