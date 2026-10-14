// The VST3 half of the identity manifest: the class id, and what the adapter's
// controller publishes, as a host reads it through getParameterInfo and
// IMidiMapping. MIDI controllers' shadow parameters are summed up in one line.

#include <MakeASound/Plugin/VST3/Adapter.h>
#include <MakeASound/Plugin/VST3/HostParameters.h>

#include "IdentityManifest.h"

#include <algorithm>
#include <array>

namespace
{
using namespace IdentityManifest;
namespace Vst = Steinberg::Vst;

std::string assignment(MakeASound::VST3::Adapter& adapter,
                       int bus,
                       int channel,
                       int controller)
{
    auto id = Vst::ParamID {0};
    auto result =
        adapter.getMidiControllerAssignment(bus,
                                            static_cast<Steinberg::int16>(channel),
                                            static_cast<Vst::CtrlNumber>(controller),
                                            id);

    if (result != Steinberg::kResultOk)
        return print("%d/%d/%d none", bus, channel, controller);

    return print("%d/%d/%d 0x%08x", bus, channel, controller, id);
}

void describeVST3(const ModuleDescription& module,
                  const PluginDescription& plugin,
                  Lines& out)
{
    auto classId = vst3ClassIdFor(module, plugin);
    out.push_back(print("vst3 class 0x%08x 0x%08x 0x%08x 0x%08x",
                        classId[0],
                        classId[1],
                        classId[2],
                        classId[3]));

    auto adapter = Steinberg::owned(
        new MakeASound::VST3::Adapter(plugin.create(),
                                      parameterIdentityFor(plugin),
                                      vst3MidiControllersFor(plugin)));
    adapter->initialize(nullptr);

    const auto& list = adapter->plugin().parameters();
    auto others = 0;
    auto lowest = ~0u;
    auto highest = 0u;
    auto allFlags = ~0;
    auto anyFlags = 0;

    for (auto index = 0; index < adapter->getParameterCount(); ++index)
    {
        auto info = Vst::ParameterInfo {};
        adapter->getParameterInfo(index, info);

        auto entry = list.indexOfHostId(info.id);

        if (entry >= 0 && list.isRegistered(entry))
        {
            out.push_back(print("vst3 param %d entry %d id %u steps %d default %.9g "
                                "flags 0x%x unit %d",
                                index,
                                entry,
                                info.id,
                                info.stepCount,
                                info.defaultNormalizedValue,
                                info.flags,
                                info.unitId));
            continue;
        }

        ++others;
        lowest = std::min(lowest, static_cast<unsigned>(info.id));
        highest = std::max(highest, static_cast<unsigned>(info.id));
        allFlags &= info.flags;
        anyFlags |= info.flags;
    }

    if (others > 0)
        out.push_back(print("vst3 controllers %d ids 0x%08x..0x%08x flags 0x%x/0x%x",
                            others,
                            lowest,
                            highest,
                            allFlags,
                            anyFlags));
    else
        out.push_back("vst3 controllers 0");

    auto mapped = std::string {};

    for (auto [bus, channel, controller]: {std::array {0, 0, 0},
                                           std::array {0, 0, 1},
                                           std::array {0, 0, 127},
                                           std::array {0, 0, 128},
                                           std::array {0, 0, 129},
                                           std::array {0, 0, 130},
                                           std::array {0, 15, 127},
                                           std::array {0, 15, 129},
                                           std::array {1, 0, 1}})
        mapped += " " + assignment(*adapter, bus, channel, controller);

    out.push_back("vst3 midimap" + mapped);

    adapter->terminate();
}

auto added = addSection({{"vst3"}, describeVST3});
} // namespace
