// The vendored VST3 SDK compiles and links on every platform the suite runs on:
// a parameter container, a FUID and a string round trip through the parts the
// adapter will build on. Nothing here opens a plugin.

#include <public.sdk/source/vst/vstparameters.h>
#include <public.sdk/source/vst/utility/stringconvert.h>
#include <pluginterfaces/base/funknown.h>

#include <NanoTest/NanoTest.h>

#include <string>

using namespace nano;

namespace
{
auto tParameters = test("VST3SDK/rangeParameterMapsPlainAndNormalized") = []
{
    using namespace Steinberg::Vst;

    auto container = ParameterContainer {};
    container.addParameter(new RangeParameter(STR16("Gain"), 7, STR16("dB"),
                                              -60., 0., -6.));

    check(container.getParameterCount() == 1);

    auto* param = container.getParameterByIndex(0);
    check(param != nullptr);
    check(param->getInfo().id == 7);
    check(param->toPlain(0.5) == -30.);
    check(param->toNormalized(-6.) == 0.9);
};

auto tFuid = test("VST3SDK/fuidRoundTripsThroughItsString") = []
{
    auto uid = Steinberg::FUID {0x12345678, 0x9ABCDEF0, 0x0F1E2D3C, 0x4B5A6978};

    char text[33] = {};
    uid.toString(text);

    auto other = Steinberg::FUID {};
    check(other.fromString(text));
    check(other == uid);
};

auto tStrings = test("VST3SDK/utf8RoundTripsThroughUtf16") = []
{
    using namespace Steinberg::Vst;

    auto utf8 = std::string {"Osc 1 / Attack \xC3\xA9"};
    auto utf16 = StringConvert::convert(utf8);
    check(utf16.size() == 16);
    check(StringConvert::convert(utf16) == utf8);
};
} // namespace
