#include <MakeASound/MakeASound.h>

int main()
{
    MakeASound::DeviceManager manager;

    auto dv = manager.manager.getDeviceNames();
    int x = 0;
    return 0;
}