#include <MakeASound/MakeASound.h>

int main()
{
    MakeASound::DeviceManager manager;

    auto config = manager.getDefaultConfig();
    auto stream = manager.openStream(config);

   // auto config = MakeASound::createDefaultConfig()

    // config

    int x = 0;
    return 0;
}