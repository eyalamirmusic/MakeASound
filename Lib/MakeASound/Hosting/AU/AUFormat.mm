#include "AUFormat.h"

#include <eacp/Core/Threads/EventLoop.h>

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace MakeASound::Hosting
{

namespace
{
// How long a component gets to answer AudioComponentInstantiate before the scan
// reports a clean error. Well inside a worker's 30 s watchdog, so a slow
// component produces a described failure rather than a killed worker.
constexpr auto instantiateTimeout = eacp::Time::MS {15000};

constexpr auto hostableTypes = std::array<OSType, 4> {kAudioUnitType_Effect,
                                                      kAudioUnitType_MusicDevice,
                                                      kAudioUnitType_MusicEffect,
                                                      kAudioUnitType_MIDIProcessor};

bool isCodeCharacter(char character) noexcept
{
    const auto value = static_cast<unsigned char>(character);

    return value >= 0x20 && value < 0x7f && character != ':';
}

std::string fourCharString(OSType code)
{
    return {static_cast<char>((code >> 24) & 0xff),
            static_cast<char>((code >> 16) & 0xff),
            static_cast<char>((code >> 8) & 0xff),
            static_cast<char>(code & 0xff)};
}

OSType fourCharCode(std::string_view text) noexcept
{
    auto code = OSType {0};

    for (auto character: text)
        code = (code << 8) | static_cast<unsigned char>(character);

    return code;
}

std::string componentIdentifier(const AudioComponentDescription& description)
{
    return fourCharString(description.componentType) + ":"
           + fourCharString(description.componentSubType) + ":"
           + fourCharString(description.componentManufacturer);
}

// Zero flags and mask: matches any component with the triple.
std::optional<AudioComponentDescription> parseIdentifier(std::string_view text)
{
    if (!looksLikeAudioUnitIdentifier(text))
        return std::nullopt;

    auto description = AudioComponentDescription();
    description.componentType = fourCharCode(text.substr(0, 4));
    description.componentSubType = fourCharCode(text.substr(5, 4));
    description.componentManufacturer = fourCharCode(text.substr(10, 4));

    return description;
}

AudioComponent findComponent(std::string_view identifier)
{
    const auto description = parseIdentifier(identifier);

    if (!description.has_value())
        return nullptr;

    return AudioComponentFindNext(nullptr, &*description);
}

std::string stringFromCF(CFStringRef text)
{
    if (text == nullptr)
        return {};

    const auto length = CFStringGetLength(text);
    const auto maxBytes =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;

    auto buffer = std::string(static_cast<std::size_t>(maxBytes), '\0');

    if (!CFStringGetCString(text, buffer.data(), maxBytes, kCFStringEncodingUTF8))
        return {};

    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    return buffer;
}

// An OSStatus as a person can read it: the four-character code Core Audio returns
// when it is one, the number otherwise.
std::string describeStatus(OSStatus status)
{
    const auto asCode = fourCharString(static_cast<OSType>(status));

    if (std::all_of(asCode.begin(), asCode.end(), isCodeCharacter))
        return "'" + asCode + "'";

    return std::to_string(static_cast<int>(status));
}

std::string subCategoriesForType(OSType type)
{
    switch (type)
    {
        case kAudioUnitType_MusicDevice:
            return "Instrument";
        case kAudioUnitType_MusicEffect:
            return "Fx|MusicEffect";
        case kAudioUnitType_MIDIProcessor:
            return "Fx|MidiEffect";
        default:
            return "Fx";
    }
}

bool typeConsumesMidi(OSType type) noexcept
{
    return type == kAudioUnitType_MusicDevice || type == kAudioUnitType_MusicEffect
           || type == kAudioUnitType_MIDIProcessor;
}

// AudioComponentCopyName answers "Manufacturer: Name". Split on the FIRST ": ", so
// a product whose own name holds one keeps it.
void splitComponentName(const std::string& full,
                        std::string& vendor,
                        std::string& name)
{
    const auto separator = full.find(": ");

    if (separator == std::string::npos)
    {
        vendor.clear();
        name = full;
        return;
    }

    vendor = full.substr(0, separator);
    name = full.substr(separator + 2);
}

// Core Audio packs a version as 0xMMMMmmbb.
std::string versionString(UInt32 version)
{
    return std::to_string((version >> 16) & 0xffff) + "."
           + std::to_string((version >> 8) & 0xff) + "."
           + std::to_string(version & 0xff);
}

// Total channels across every element (bus) of one scope.
int countChannels(AudioComponentInstance instance, AudioUnitScope scope)
{
    auto elements = UInt32 {0};
    auto size = UInt32 {sizeof(elements)};

    if (AudioUnitGetProperty(
            instance, kAudioUnitProperty_ElementCount, scope, 0, &elements, &size)
        != noErr)
        return 0;

    auto total = 0;

    for (auto element = UInt32 {0}; element < elements; ++element)
    {
        auto format = AudioStreamBasicDescription();
        auto formatSize = UInt32 {sizeof(format)};

        if (AudioUnitGetProperty(instance,
                                 kAudioUnitProperty_StreamFormat,
                                 scope,
                                 element,
                                 &format,
                                 &formatSize)
            == noErr)
            total += static_cast<int>(format.mChannelsPerFrame);
    }

    return total;
}

// A view of the component's own: kAudioUnitProperty_CocoaUI, which an AUv3 behind
// the v2 bridge answers too. Apple's generic view is the host's to draw and
// doesn't count.
bool hasCustomEditor(AudioComponentInstance instance)
{
    auto size = UInt32 {0};
    auto writable = Boolean {0};

    const auto status = AudioUnitGetPropertyInfo(instance,
                                                 kAudioUnitProperty_CocoaUI,
                                                 kAudioUnitScope_Global,
                                                 0,
                                                 &size,
                                                 &writable);

    return status == noErr && size > 0;
}

// One instantiate attempt, shared with the completion handler, which can outlive
// a timed-out wait: `abandoned` tells a late arrival to dispose the instance.
struct InstantiateState
{
    AudioComponentInstance instance = nullptr;
    OSStatus status = noErr;
    bool finished = false;
    bool abandoned = false;
};

// The asynchronous form, the only one that can create an AUv3 in another
// process, pumping the event loop until its answer lands on the main queue.
// Options 0, as a DAW asks: a v2 loads in process, a v3 extension stays in its
// own.
std::shared_ptr<InstantiateState> instantiate(AudioComponent component)
{
    auto state = std::make_shared<InstantiateState>();

    AudioComponentInstantiate(
        component,
        0,
        ^(AudioComponentInstance instance, OSStatus status) {
          dispatch_async(dispatch_get_main_queue(),
                         ^{
                           if (state->abandoned)
                           {
                               if (instance != nullptr)
                                   AudioComponentInstanceDispose(instance);

                               return;
                           }

                           state->instance = instance;
                           state->status = status;
                           state->finished = true;
                         });
        });

    eacp::Threads::runEventLoopUntil([state] { return state->finished; },
                                     instantiateTimeout);

    if (!state->finished)
        state->abandoned = true;

    return state;
}
} // namespace

bool looksLikeAudioUnitIdentifier(std::string_view text) noexcept
{
    if (text.size() != 14 || text[4] != ':' || text[9] != ':')
        return false;

    for (auto start: {0, 5, 10})
        for (auto i = start; i < start + 4; ++i)
            if (!isCodeCharacter(text[static_cast<std::size_t>(i)]))
                return false;

    return true;
}

PluginFormat AUFormat::format() const
{
    return PluginFormat::AU;
}

Vector<std::string> AUFormat::defaultSearchFolders() const
{
    return {};
}

Vector<std::string> AUFormat::findTargets(const Vector<std::string>&) const
{
    // A set: one component can be registered twice (a stale copy in ~/Library
    // beside a newer one in /Library), and sorting makes two sweeps agree.
    auto found = std::set<std::string>();

    for (auto type: hostableTypes)
    {
        auto wildcard = AudioComponentDescription();
        wildcard.componentType = type;

        auto component = AudioComponentFindNext(nullptr, &wildcard);

        while (component != nullptr)
        {
            auto description = AudioComponentDescription();

            if (AudioComponentGetDescription(component, &description) == noErr)
                found.insert(componentIdentifier(description));

            component = AudioComponentFindNext(component, &wildcard);
        }
    }

    auto result = Vector<std::string>();

    for (const auto& identifier: found)
        result.add(identifier);

    return result;
}

bool AUFormat::ownsTarget(std::string_view target) const
{
    return looksLikeAudioUnitIdentifier(target);
}

bool AUFormat::targetExists(std::string_view target) const
{
    return findComponent(target) != nullptr;
}

ModuleStamp AUFormat::stamp(std::string_view target) const
{
    auto stamp = ModuleStamp();
    auto component = findComponent(target);
    auto version = UInt32 {0};

    if (component != nullptr
        && AudioComponentGetVersion(component, &version) == noErr)
        stamp.lastModified = static_cast<std::int64_t>(version);

    return stamp;
}

ModuleScan AUFormat::scan(std::string_view target) const
{
    auto scan = ModuleScan();
    scan.format = PluginFormat::AU;
    scan.path = std::string(target);

    const auto description = parseIdentifier(target);

    if (!description.has_value())
    {
        scan.error = "not an Audio Unit identifier (type:subtype:manufacturer)";
        return scan;
    }

    auto component = findComponent(target);

    if (component == nullptr)
    {
        scan.error = "no Audio Unit is registered as " + scan.path;
        return scan;
    }

    scan.setStamp(stamp(target));

    auto plugin = PluginDescription();
    plugin.format = PluginFormat::AU;
    plugin.path = scan.path;
    plugin.classId = scan.path;
    plugin.category = fourCharString(description->componentType);
    plugin.subCategories = subCategoriesForType(description->componentType);
    plugin.hasMidiInput = typeConsumesMidi(description->componentType);

    auto name = CFStringRef {nullptr};

    if (AudioComponentCopyName(component, &name) == noErr && name != nullptr)
    {
        splitComponentName(stringFromCF(name), plugin.vendor, plugin.name);
        CFRelease(name);
    }

    auto version = UInt32 {0};

    if (AudioComponentGetVersion(component, &version) == noErr)
        plugin.version = versionString(version);

    const auto instantiated = instantiate(component);

    if (!instantiated->finished)
    {
        scan.error = "the component did not answer AudioComponentInstantiate within "
                     + std::to_string(instantiateTimeout.count) + " ms";
        return scan;
    }

    if (instantiated->status != noErr || instantiated->instance == nullptr)
    {
        scan.error = "the component could not be instantiated ("
                     + describeStatus(instantiated->status) + ")";
        return scan;
    }

    plugin.numAudioInputs =
        countChannels(instantiated->instance, kAudioUnitScope_Input);
    plugin.numAudioOutputs =
        countChannels(instantiated->instance, kAudioUnitScope_Output);
    plugin.hasEditor = hasCustomEditor(instantiated->instance);

    // Never initialized: all of the above reads uninitialized, and initializing
    // allocates render resources for a plugin about to be thrown away.
    AudioComponentInstanceDispose(instantiated->instance);
    instantiated->instance = nullptr;

    scan.classes.add(std::move(plugin));
    return scan;
}

} // namespace MakeASound::Hosting
