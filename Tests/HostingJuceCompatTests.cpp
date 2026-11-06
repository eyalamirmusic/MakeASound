// What a JUCE 8.0.12 host called the plugins it hosted (Hosting/JuceHostCompat.h),
// from a cache row, against ids JUCE's own host computed for Modalics' plugins
// (its VST3 moduleinfo.json class ids and AU component codes beside the
// PluginDescription JUCE filled in for each).

#include <MakeASound/Plugin/VST3/VST3Common.h>

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <public.sdk/source/vst/utility/uid.h>

#include <NanoTest/NanoTest.h>

namespace HostingJuceCompatTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Compat;

namespace
{
Hosting::PluginDescription row(PluginFormat format, std::string classId)
{
    auto description = Hosting::PluginDescription {};
    description.format = format;
    description.classId = std::move(classId);
    description.path = description.classId;
    return description;
}

// JUCE's deprecatedUid hashes the raw TUID, which Windows lays out COM-style: the
// value there is the same rule over the swapped bytes.
int32_t onThisPlatform(int32_t textOrder, int32_t comOrder)
{
    return ::VST3::UID::defaultComFormat ? comOrder : textOrder;
}

auto tVST3 = test("HostingJuceCompat/aVST3IdIsJuceHostsHash") = []
{
    // Beat Scholar: CID ABCDEF01 9182FAEB 4D6F6458 42545343, which JUCE's host
    // described as uniqueId -247886987 (f1398b75), deprecatedUid 2128708814.
    auto beatScholar = row(PluginFormat::VST3, "ABCDEF019182FAEB4D6F645842545343");
    check(juceHostedUniqueId(beatScholar) == -247886987);
    check(juceHostedDeprecatedUid(beatScholar)
          == onThisPlatform(2128708814, -1153442590));

    // Plugin Buddy: uniqueId -12084053, deprecatedUid 2129139923.
    auto pluginBuddy = row(PluginFormat::VST3, "ABCDEF019182FAEB4D6F645850626479");
    check(juceHostedUniqueId(pluginBuddy) == -12084053);
    check(juceHostedDeprecatedUid(pluginBuddy)
          == onThisPlatform(2129139923, -1153011481));

    // The class id's case does not matter; its shape does.
    check(juceHostedUniqueId(
              row(PluginFormat::VST3, "abcdef019182faeb4d6f645842545343"))
          == -247886987);
    check(juceHostedUniqueId(row(PluginFormat::VST3, "ABCDEF01")) == 0);
    check(juceHostedUniqueId(
              row(PluginFormat::VST3, "ABCDEF019182FAEB4D6F6458425453ZZ"))
          == 0);
    check(juceHostedDeprecatedUid(row(PluginFormat::VST3, "")) == 0);
};

auto tAU = test("HostingJuceCompat/anAudioUnitIdIsTheCodesXored") = []
{
    // Beat Scholar aumu:BTSC:ModX and Plugin Buddy FX aumf:Pbfx:ModX, which
    // JUCE's host described as 1850628718 and 2088267590, both ids alike.
    auto beatScholar = row(PluginFormat::AU, "aumu:BTSC:ModX");
    check(juceHostedUniqueId(beatScholar) == 1850628718);
    check(juceHostedDeprecatedUid(beatScholar) == 1850628718);

    auto pluginBuddyFX = row(PluginFormat::AU, "aumf:Pbfx:ModX");
    check(juceHostedUniqueId(pluginBuddyFX) == 2088267590);
    check(juceHostedDeprecatedUid(pluginBuddyFX) == 2088267590);

    // A code with a space, as Apple's DLS synth has.
    check(juceHostedUniqueId(row(PluginFormat::AU, "aumu:dls :appl"))
          == static_cast<int32_t>(0x61756d75u ^ 0x646c7320u ^ 0x6170706cu));

    check(juceHostedUniqueId(row(PluginFormat::AU, "aumu:BTSC")) == 0);
    check(juceHostedUniqueId(row(PluginFormat::AAX, "aumu:BTSC:ModX")) == 0);
};

auto tTreeOrder = test("HostingJuceCompat/aGroupGathersWhereItFirstAppears") = []
{
    using Paths = Vector<Vector<int64_t>>;

    // No groups, and contiguous groups: the plugin's own order.
    check(juceParameterTreeOrder(Paths {}).empty());
    check(juceParameterTreeOrder(Paths {{}, {}, {}}) == Vector<int> {0, 1, 2});
    check(juceParameterTreeOrder(Paths {{}, {1}, {1}, {2}, {}})
          == Vector<int> {0, 1, 2, 3, 4});

    // Interleaved: group 1 sits where its first parameter was and takes its
    // later ones; 2 likewise.
    check(juceParameterTreeOrder(Paths {{1}, {}, {2}, {1}, {}, {2}})
          == Vector<int> {0, 3, 1, 2, 5, 4});

    // Nested: 2 inside 1 is created inside 1 when its first parameter comes, so
    // after the 1-parameters listed before it; a root parameter in between
    // stays after the whole of 1.
    check(juceParameterTreeOrder(Paths {{1}, {}, {1, 2}, {1}, {1, 2}})
          == Vector<int> {0, 2, 4, 3, 1});

    // Group ids are only names: 0 is a group like any other.
    check(juceParameterTreeOrder(Paths {{0}, {}, {0}}) == Vector<int> {0, 2, 1});
};
} // namespace
} // namespace HostingJuceCompatTests
