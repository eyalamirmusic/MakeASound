#include "../../Plugin/VST3/VST3Common.h"
#include "VST3Format.h"
#include "VST3Classes.h"
#include "../Scan/Scanner.h"

#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>

#include <cstring>

namespace MakeASound::Hosting
{

namespace
{
using namespace Steinberg;
using namespace Steinberg::Vst;

// Total channels across every bus of one direction. A bus count alone says "1 in,
// 1 out" for a mono and a 5.1 plugin alike; channels are what a host wires up.
int countChannels(IComponent& component, BusDirection direction)
{
    auto total = 0;
    const auto numBuses = component.getBusCount(kAudio, direction);

    for (auto i = int32 {0}; i < numBuses; ++i)
    {
        auto bus = BusInfo();

        if (component.getBusInfo(kAudio, direction, i, bus) == kResultOk)
            total += bus.channelCount;
    }

    return total;
}

// Instantiates one class just long enough to ask about its buses and editor.
// Lighter than a load: no connection points and no state, since the view is
// never attached and so never reads them.
void describeClass(const ::VST3::Hosting::PluginFactory& factory,
                   FUnknown* hostContext,
                   const ::VST3::Hosting::ClassInfo& audioClass,
                   PluginDescription& description)
{
    auto component = factory.createInstance<IComponent>(audioClass.ID());

    if (!component || component->initialize(hostContext) != kResultOk)
        return;

    description.numAudioInputs = countChannels(*component, kInput);
    description.numAudioOutputs = countChannels(*component, kOutput);
    description.hasMidiInput = component->getBusCount(kEvent, kInput) > 0;

    // A dual-component plugin names a separate controller class; a single
    // component answers IEditController itself.
    auto controller = IPtr<IEditController>();
    auto separateController = false;
    TUID controllerId = {};

    if (component->getControllerClassId(controllerId) == kResultOk
        && std::memcmp(controllerId, audioClass.ID().data(), sizeof(TUID)) != 0)
    {
        controller = factory.createInstance<IEditController>(
            ::VST3::UID::fromTUID(controllerId));

        if (controller && controller->initialize(hostContext) == kResultOk)
            separateController = true;
        else
            controller = nullptr;
    }

    if (!controller)
        if (auto fromComponent = FUnknownPtr<IEditController>(component))
            controller = fromComponent;

    if (controller)
    {
        // There is no query for "has a GUI": asking for the view is the only way.
        if (auto* view = controller->createView(ViewType::kEditor))
        {
            description.hasEditor = true;
            view->release();
        }

        if (separateController)
            controller->terminate();
    }

    controller = nullptr;
    component->terminate();
}
} // namespace

PluginFormat VST3Format::format() const
{
    return PluginFormat::VST3;
}

Vector<std::string> VST3Format::findTargets(const Vector<std::string>& folders) const
{
    return findModulesWithExtension(folders, ".vst3");
}

bool VST3Format::ownsTarget(std::string_view target) const
{
    return Text::endsWithIgnoringCase(target, ".vst3");
}

ModuleScan VST3Format::scan(std::string_view target) const
{
    auto scan = ModuleScan();
    scan.format = PluginFormat::VST3;
    scan.path = std::string(target);
    scan.setStamp(stamp(target));

    auto error = std::string();
    auto module = ::VST3::Hosting::Module::create(scan.path, error);

    if (!module)
    {
        scan.error = error.empty() ? "the module failed to load" : error;
        return scan;
    }

    const auto& factory = module->getFactory();

    // Outlives every initialize() below.
    auto hostApplication = owned(new HostApplication());
    factory.setHostContext(hostApplication);

    for (const auto& audioClass: audioClassesOf(factory))
    {
        auto description = PluginDescription();
        description.format = PluginFormat::VST3;
        description.path = scan.path;
        description.classId = classIdOf(audioClass);
        description.name = audioClass.name();
        description.vendor = audioClass.vendor();
        description.version = audioClass.version();
        description.category = audioClass.category();
        description.subCategories = audioClass.subCategoriesString();

        describeClass(factory, hostApplication, audioClass, description);

        scan.classes.add(std::move(description));
    }

    // A module with no audio-effect class is not a failure: it caches as a good
    // "nothing here", so it is not rescanned every launch.
    return scan;
}

} // namespace MakeASound::Hosting
