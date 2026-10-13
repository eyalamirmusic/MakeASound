// Compiled into MakeASoundAUTestModule beside the module's own entry, so C++23
// over the AudioUnitSDK: the factory the AU suites register JucePort's two
// descriptions with, the adapter built over the description each subtype names
// rather than one of describeModule()'s, and dispatched as EntryPoint.cpp
// dispatches, auType included.
#include <MakeASound/Plugin/AU/Adapter.h>
#include <MakeASound/Plugin/AU/Factory.h>

#include "JucePort.h"

extern "C" void* MakeASoundAUTestPortFactory(const AudioComponentDescription* desc);

namespace
{
using namespace MakeASound;

const PluginDescription& portDescriptionOf(AudioComponentInstance instance)
{
    auto description = AudioComponentDescription {};
    AudioComponentGetDescription(AudioComponentInstanceGetComponent(instance),
                                 &description);

    const auto* plugin = JucePort::descriptionFor(description.componentSubType);

    if (plugin == nullptr)
        ausdk::Throw(kAudioUnitErr_InvalidProperty);

    return *plugin;
}

struct PortAdapter : AU::Adapter
{
    explicit PortAdapter(AudioComponentInstance instance)
        : Adapter(instance, portDescriptionOf(instance))
    {
    }
};
} // namespace

extern "C" void* MakeASoundAUTestPortFactory(const AudioComponentDescription* desc)
{
    if (desc == nullptr
        || desc->componentManufacturer != JucePort::manufacturer.toUint32())
        return nullptr;

    return AU::dispatchFactory<PortAdapter>(
        desc, JucePort::descriptionFor(desc->componentSubType));
}
