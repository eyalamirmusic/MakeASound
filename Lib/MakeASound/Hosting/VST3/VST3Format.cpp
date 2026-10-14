#include "../../Plugin/VST3/VST3Common.h"
#include "VST3Format.h"
#include "VST3Classes.h"
#include "VST3HostedPlugin.h"
#include "../Scan/Scanner.h"

#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/utility/stringconvert.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstmessage.h>

#include <cstring>
#include <filesystem>

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

// The SDK's host application under the host's own name.
class NamedHostApplication : public HostApplication
{
public:
    explicit NamedHostApplication(std::string nameToUse)
        : name(std::move(nameToUse))
    {
    }

    tresult PLUGIN_API getName(String128 out) override
    {
        return StringConvert::convert(name, out) ? kResultTrue : kInternalError;
    }

private:
    std::string name;
};

// Some plugins find their own files through the working directory while they
// load and initialise, and JUCE's host enters the bundle's folder for that span.
class ScopedWorkingDirectory
{
public:
    explicit ScopedWorkingDirectory(const std::string& bundle)
    {
        namespace fs = std::filesystem;

        auto path = fs::path(bundle);
        auto error = std::error_code();
        previous = fs::current_path(error);

        if (error || !path.is_absolute())
            return;

        fs::current_path(path.parent_path(), error);
        entered = !error;
    }

    ~ScopedWorkingDirectory()
    {
        auto error = std::error_code();

        if (entered)
            std::filesystem::current_path(previous, error);
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

private:
    std::filesystem::path previous;
    bool entered = false;
};

LoadResult failure(std::string error)
{
    auto result = LoadResult();
    result.error = std::move(error);
    return result;
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

LoadResult VST3Format::load(const PluginDescription& description,
                            const HostInfo& host) const
{
    const auto workingDirectory = ScopedWorkingDirectory(description.path);

    auto error = std::string();
    auto module = ::VST3::Hosting::Module::create(description.path, error);

    if (!module)
        return failure(error.empty() ? "the module failed to load" : error);

    const auto& factory = module->getFactory();
    auto chosen = std::optional<::VST3::Hosting::ClassInfo>();

    for (auto& audioClass: audioClassesOf(factory))
        if (classIdOf(audioClass) == description.classId)
            chosen = std::move(audioClass);

    if (!chosen)
        return failure("the module has no audio class with id "
                       + description.classId);

    // Outlives every initialize() below; the plugin keeps it.
    auto hostApplication =
        IPtr<IHostApplication>(owned(new NamedHostApplication(host.name)));
    factory.setHostContext(hostApplication);

    auto component = factory.createInstance<IComponent>(chosen->ID());

    if (!component)
        return failure("the factory could not create the component");

    if (component->initialize(hostApplication) != kResultOk)
        return failure("the component did not initialize");

    // A controller of its own when the component names another class; the
    // component itself otherwise.
    auto controller = IPtr<IEditController>();
    auto separate = false;
    TUID controllerId = {};

    if (component->getControllerClassId(controllerId) == kResultOk
        && std::memcmp(controllerId, chosen->ID().data(), sizeof(TUID)) != 0)
    {
        controller = factory.createInstance<IEditController>(
            ::VST3::UID::fromTUID(controllerId));

        if (controller && controller->initialize(hostApplication) == kResultOk)
            separate = true;
        else
            controller = nullptr;
    }

    if (!controller)
        if (auto fromComponent = FUnknownPtr<IEditController>(component))
            controller = fromComponent;

    // Connected, and given the component's state, before the plugin is built or
    // its view asked for: some plugins read that state in attached().
    if (separate)
    {
        auto componentPoint = FUnknownPtr<IConnectionPoint>(component);
        auto controllerPoint = FUnknownPtr<IConnectionPoint>(controller);

        if (componentPoint && controllerPoint)
        {
            componentPoint->connect(controllerPoint);
            controllerPoint->connect(componentPoint);
        }

        auto stream = MemoryStream();

        if (component->getState(&stream) == kResultOk)
        {
            stream.seek(0, IBStream::kIBSeekSet, nullptr);
            controller->setComponentState(&stream);
        }
    }

    auto loaded = description;
    loaded.format = PluginFormat::VST3;

    auto result = LoadResult();
    result.plugin = EA::makeOwned<VST3HostedPlugin>(std::move(loaded),
                                                    std::move(module),
                                                    std::move(hostApplication),
                                                    std::move(component),
                                                    std::move(controller),
                                                    separate);
    result.plugin->setEditorFactory(host.editors);
    return result;
}

} // namespace MakeASound::Hosting
