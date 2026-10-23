// Compiled beside a test binary's AU entry (MakeASoundAUTestModule, and each
// makeasound_add_allocation_tests module), so C++23 over the AudioUnitSDK: the
// plugin behind a unit, asked of its adapter through the property the view
// factory reads, for the C++20 suites that may not include the adapter.
#include <MakeASound/Plugin/AU/Adapter.h>
#include <MakeASound/Plugin/AU/CocoaUI.h>

extern "C" MakeASound::Plugin* MakeASoundAUTestPluginOf(AudioUnit unit)
{
    using namespace MakeASound;

    auto* adapter = static_cast<AU::Adapter*>(nullptr);
    auto size = UInt32 {sizeof(adapter)};

    if (AudioUnitGetProperty(
            unit, AU::adapterProperty, kAudioUnitScope_Global, 0, &adapter, &size)
            != noErr
        || adapter == nullptr)
        return nullptr;

    return &adapter->plugin();
}
