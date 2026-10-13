// Both Info.plist forms MakeASoundAUDescribe writes from describeModule(): the v2
// .component's and the AUv3 .appex's, read back through Core Foundation as the
// component registry reads them.

#include "TestPlugins.h"

#include <MakeASound/Plugin/AU/ComponentType.h>
#include <MakeASound/Plugin/AU/Plist.h>

#include <NanoTest/NanoTest.h>

#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

using namespace nano;
using namespace MakeASound;

namespace
{
// A parsed plist, released on the way out.
struct Plist
{
    explicit Plist(const std::filesystem::path& path)
    {
        auto file = std::ifstream(path, std::ios::binary);
        auto bytes = std::string(std::istreambuf_iterator<char>(file), {});
        auto* data = CFDataCreate(kCFAllocatorDefault,
                                  reinterpret_cast<const UInt8*>(bytes.data()),
                                  static_cast<CFIndex>(bytes.size()));

        root = CFPropertyListCreateWithData(
            kCFAllocatorDefault, data, kCFPropertyListImmutable, nullptr, nullptr);
        CFRelease(data);
    }

    ~Plist()
    {
        if (root != nullptr)
            CFRelease(root);
    }

    Plist(const Plist&) = delete;
    Plist& operator=(const Plist&) = delete;

    CFDictionaryRef dictionary() const
    {
        return root != nullptr && CFGetTypeID(root) == CFDictionaryGetTypeID()
                   ? static_cast<CFDictionaryRef>(root)
                   : nullptr;
    }

    CFPropertyListRef root = nullptr;
};

CFTypeRef valueOf(CFDictionaryRef dictionary, const char* key)
{
    if (dictionary == nullptr)
        return nullptr;

    auto* name =
        CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    auto* value = CFDictionaryGetValue(dictionary, name);
    CFRelease(name);
    return value;
}

std::string stringOf(CFDictionaryRef dictionary, const char* key)
{
    auto value = valueOf(dictionary, key);

    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID())
        return {};

    auto buffer = std::array<char, 256> {};
    CFStringGetCString(static_cast<CFStringRef>(value),
                       buffer.data(),
                       buffer.size(),
                       kCFStringEncodingUTF8);
    return buffer.data();
}

CFDictionaryRef dictionaryOf(CFDictionaryRef dictionary, const char* key)
{
    auto value = valueOf(dictionary, key);
    return value != nullptr && CFGetTypeID(value) == CFDictionaryGetTypeID()
               ? static_cast<CFDictionaryRef>(value)
               : nullptr;
}

CFArrayRef arrayOf(CFDictionaryRef dictionary, const char* key)
{
    auto value = valueOf(dictionary, key);
    return value != nullptr && CFGetTypeID(value) == CFArrayGetTypeID()
               ? static_cast<CFArrayRef>(value)
               : nullptr;
}

CFDictionaryRef componentAt(CFArrayRef components, CFIndex index)
{
    return static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(components, index));
}

bool isNumber(CFDictionaryRef dictionary, const char* key, long expected)
{
    auto value = valueOf(dictionary, key);
    auto number = 0l;

    return value != nullptr && CFGetTypeID(value) == CFNumberGetTypeID()
           && CFNumberGetValue(
               static_cast<CFNumberRef>(value), kCFNumberLongType, &number)
           && number == expected;
}

bool isTrue(CFDictionaryRef dictionary, const char* key)
{
    auto value = valueOf(dictionary, key);
    return value != nullptr && CFGetTypeID(value) == CFBooleanGetTypeID()
           && CFBooleanGetValue(static_cast<CFBooleanRef>(value));
}

std::filesystem::path scratchFile(const char* name)
{
    return std::filesystem::temp_directory_path() / name;
}

auto tTags = test("AUPlist/componentTagsFollowTheCategory") = []
{
    check(AU::componentTag(Category::Instrument) == "Synthesizer");
    check(AU::componentTag(Category::MidiEffect) == "MIDI");
    check(AU::componentTag(Category::Effect) == "Effects");
};

auto tComponent = test("AUPlist/theComponentPlistNamesTheFactory") = []
{
    auto path = scratchFile("MakeASoundTests-component.plist");
    check(AU::writeAudioComponentsPlist(
        describeModule(), "Tests", "com.makeasound.tests.component", "Tests", path));

    auto plist = Plist(path);
    auto* root = plist.dictionary();
    check(root != nullptr);
    check(stringOf(root, "CFBundlePackageType") == "BNDL");
    check(stringOf(root, "CFBundleIdentifier") == "com.makeasound.tests.component");
    check(stringOf(root, "CFBundleVersion") == "1.0.0");

    auto* components = arrayOf(root, "AudioComponents");
    check(components != nullptr && CFArrayGetCount(components) == 2);

    auto* gain = componentAt(components, 0);
    check(stringOf(gain, "type") == "aufx");
    check(stringOf(gain, "subtype") == "Gain");
    check(stringOf(gain, "manufacturer") == "MaSo");
    check(stringOf(gain, "name") == "MakeASound Tests: Gain");
    check(stringOf(gain, "factoryFunction") == "MakeASoundAUFactory");
    check(isNumber(gain, "version", 0x010000));
    check(isTrue(gain, "sandboxSafe"));
    check(valueOf(gain, "tags") == nullptr);

    std::filesystem::remove(path);
};

auto tAppex = test("AUPlist/theAppExtensionPlistDeclaresItsComponents") = []
{
    auto path = scratchFile("MakeASoundTests-appex.plist");
    auto bundle = AU::AppExtensionPlist {.bundleName = "MakeASound Tests",
                                         .bundleId = "com.makeasound.tests.auv3",
                                         .executable = "MakeASoundTests",
                                         .principalClass = "TestsViewController"};
    check(AU::writeAppExtensionPlist(describeModule(), bundle, path));

    auto plist = Plist(path);
    auto* root = plist.dictionary();
    check(root != nullptr);
    check(stringOf(root, "CFBundlePackageType") == "XPC!");
    check(stringOf(root, "CFBundleIdentifier") == "com.makeasound.tests.auv3");
    check(stringOf(root, "CFBundleExecutable") == "MakeASoundTests");
    check(stringOf(root, "CFBundleDisplayName") == "MakeASound Tests");
    check(stringOf(root, "CFBundleVersion") == "1.0.0");

    auto* extension = dictionaryOf(root, "NSExtension");
    check(stringOf(extension, "NSExtensionPointIdentifier")
          == "com.apple.AudioUnit-UI");
    check(stringOf(extension, "NSExtensionPrincipalClass") == "TestsViewController");

    auto* components =
        arrayOf(dictionaryOf(extension, "NSExtensionAttributes"), "AudioComponents");
    check(components != nullptr && CFArrayGetCount(components) == 2);

    auto* gain = componentAt(components, 0);
    check(stringOf(gain, "type") == "aufx");
    check(stringOf(gain, "subtype") == "Gain");
    check(stringOf(gain, "manufacturer") == "MaSo");
    check(stringOf(gain, "description") == "Gain");
    check(isNumber(gain, "version", 0x010000));
    check(isTrue(gain, "sandboxSafe"));
    check(valueOf(gain, "factoryFunction") == nullptr);

    auto* echo = componentAt(components, 1);
    check(stringOf(echo, "type") == "aumu");

    for (auto [component, tag]: {std::pair {gain, "Effects"}, {echo, "Synthesizer"}})
    {
        auto* tags = arrayOf(component, "tags");
        check(tags != nullptr && CFArrayGetCount(tags) == 1);

        auto* first = static_cast<CFStringRef>(CFArrayGetValueAtIndex(tags, 0));
        auto* expected = CFStringCreateWithCString(
            kCFAllocatorDefault, tag, kCFStringEncodingUTF8);
        check(CFStringCompare(first, expected, 0) == kCFCompareEqualTo);
        CFRelease(expected);
    }

    // No view: the plain extension point.
    bundle.hasView = false;
    check(AU::writeAppExtensionPlist(describeModule(), bundle, path));

    auto plain = Plist(path);
    check(stringOf(dictionaryOf(plain.dictionary(), "NSExtension"),
                   "NSExtensionPointIdentifier")
          == "com.apple.AudioUnit");

    std::filesystem::remove(path);
};
} // namespace
