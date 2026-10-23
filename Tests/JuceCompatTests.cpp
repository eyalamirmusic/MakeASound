// The readers in Plugin/Compat/ against bytes JUCE 8.0.12 wrote, the fixtures in
// JuceFixtures.h (the format suites hand the same bytes to their adapters), and
// String::hashCode/hashCode64 of a few ids, which JUCE printed too.

#include "JuceFixtures.h"

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace JuceCompatTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Compat;
using namespace JuceFixtures;

namespace
{
bool near(float a, float b, float tolerance = 1e-4f)
{
    return std::abs(a - b) < tolerance;
}

auto tHash = test("JuceCompat/hashesMatchStringHashCode") = []
{
    // JUCE's String::hashCode and hashCode64 of each, as JUCE printed them.
    check(juceStringHash("") == 0u);
    check(juceStringHash64("") == 0u);
    check(juceStringHash("cutoff") == 2945903597u);
    check(juceStringHash64("cutoff") == 1052795674147ull);
    check(juceStringHash("Gain") == 2211743u);
    check(juceStringHash64("Gain") == 74151583ull);
    check(juceStringHash("attack") == 2887708232u);
    check(juceStringHash64("attack") == 1031671269632ull);
    // Octal escapes, which cannot run into the letters after them: "Ünïcødé",
    // "日本語" and "🎛knob".
    constexpr auto latin = std::string_view {"\303\234n\303\257c\303\270d\303\251"};
    constexpr auto cjk = std::string_view {"\346\227\245\346\234\254\350\252\236"};
    constexpr auto emoji = std::string_view {"\360\237\216\233knob"};

    check(juceStringHash(latin) == 1055433303u);
    check(juceStringHash64(latin) == 234715519163149ull);
    check(juceStringHash(cjk) == 25921943u);
    check(juceStringHash64(cjk) == 268796183ull);
    check(juceStringHash(emoji) == 2156592273u);
    check(juceStringHash64(emoji) == 13309332603125ull);

    constexpr auto longId =
        std::string_view {"a fairly long parameter identifier to wrap 32 bits"};
    check(juceStringHash(longId) == 1395067530u);
    check(juceStringHash64(longId) == 12994458627382894100ull);

    // A JUCE VST3 or AU parameter id.
    check((juceStringHash("cutoff") & 0x7fffffffu) == 798419949u);
};

auto tHashMalformed = test("JuceCompat/hashDecodesAsCharPointerUtf8Does") = []
{
    // A three-byte lead cut short by 'a': CharPointer_UTF8 keeps the two bytes
    // it has (0x197) and reads 'a' next. A NUL ends the string.
    check(juceStringHash("\346\227a") == 31u * 0x197u + 97u);
    check(juceStringHash(std::string_view {"Gain\0more", 9})
          == juceStringHash("Gain"));
};

auto tXmlBlob = test("JuceCompat/aCopyXmlToBinaryBlobGivesItsText") = []
{
    auto bytes = bytesOf(apvtsBlob);
    check(isJuceXmlBlob(bytes));

    auto text = juceXmlText(bytes);
    check(text.has_value());
    check(text->size() == 312);
    check(text->starts_with(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <FixturePlugin"));
    check(text->ends_with("</FixturePlugin>"));

    // A length past the bytes is clamped to them, as getXmlFromBinary does.
    auto cut = std::string {bytes.substr(0, 100)};
    check(juceXmlText(cut)->size() == 92);

    check(!isJuceXmlBlob("{\"params\":{}}"));
    check(!isJuceXmlBlob(bytes.substr(0, 8)));
    check(!juceXmlText(std::string_view {"VC2!\0\0\0\0<a/>", 12}).has_value());
};

void checkFixtureTree(const JuceTree& root)
{
    check(root.type == "FixturePlugin");
    check(root.property("version") != nullptr && *root.property("version") == "3");
    check(root.children.size() == 6);

    for (const auto& child: root.children)
        check(child.type == "PARAM");

    check(*root.children[0].property("id") == "attack");
    check(*root.children[4].property("id") == "tone&\xc3\xa9\"<>");
    check(near(
        static_cast<float>(juceStringToDouble(*root.children[1].property("value"))),
        440.f));
    check(root.child("PARAM") == &root.children[0]);
    check(root.child("NOPE") == nullptr);
    check(root.property("missing") == nullptr);
}

auto tParseXml = test("JuceCompat/theXmlReaderReadsJucesXml") = []
{
    auto root = parseJuceXml(*juceXmlText(bytesOf(apvtsBlob)));
    check(root.has_value());
    checkFixtureTree(*root);
};

auto tParseXmlForms = test("JuceCompat/theXmlReaderTakesWhatXmlAllows") = []
{
    auto text = std::string_view {
        "\xef\xbb\xbf<?xml version=\"1.0\"?>\r\n<!DOCTYPE s [<!ENTITY x \"y\">]>\n"
        "<!-- a comment --><State a='single &apos;q&apos;' b=\"&#x41;&#66;&#x1F39B;\">\n"
        "  text that a ValueTree drops <![CDATA[ <not> an element ]]>\n"
        "  <Child id = \"one\" />\n  <?pi ignored?><!-- <Hidden/> -->\n"
        "  <Child id=\"two\"><Grand x=\"&unknown; &amp\"/></Child>\n</State>\n"
        "trailing junk XmlDocument ignores"};

    auto root = parseJuceXml(text);
    check(root.has_value());
    check(root->type == "State");
    check(*root->property("a") == "single 'q'");
    check(*root->property("b") == "AB\xf0\x9f\x8e\x9b");
    check(root->children.size() == 2);
    check(*root->children[0].property("id") == "one");
    check(*root->children[1].children[0].property("x") == "&unknown; &amp");

    check(!parseJuceXml("").has_value());
    check(!parseJuceXml("not xml").has_value());
    check(!parseJuceXml("<open>").has_value());
    check(!parseJuceXml("<a b=c/>").has_value());
    check(!parseJuceXml("<a b=\"unterminated/>").has_value());
};

auto tValueTree = test("JuceCompat/theBinaryReaderReadsWriteToStream") = []
{
    auto bytes = bytesOf(apvtsValueTree);
    auto root = readJuceValueTree(bytes);
    check(root.has_value());
    checkFixtureTree(*root);

    // A double comes back exact; the trailer's bool as "1".
    check(juceStringToDouble(*root->children[1].property("value"))
          == static_cast<double>(440.00006103515625f));

    auto trailer = readJuceValueTree(bytesOf(bypassedTrailer).substr(8, 29));
    check(trailer.has_value());
    check(trailer->type == "JUCEPrivateData");
    check(*trailer->property("Bypass") == "1");

    // Bytes past the tree are ignored; a tree cut short is refused.
    check(readJuceValueTree(std::string {bytes} + "junk").has_value());
    check(!readJuceValueTree(bytes.substr(0, bytes.size() - 1)).has_value());
    check(!readJuceValueTree(bytes.substr(0, 40)).has_value());
    check(!readJuceValueTree(std::string_view {"\0\0\0", 3}).has_value());
    check(!readJuceValueTree("").has_value());
};

auto tStripTrailer = test("JuceCompat/theVST3TrailerIsStripped") = []
{
    // The results are views into what they read, so that is kept alive.
    auto bypassedState = withTrailer(blob(), bytesOf(bypassedTrailer));
    auto bypassed = stripJucePrivateData(bypassedState);
    check(bypassed.pluginState == bytesOf(apvtsBlob));
    check(bypassed.bypass == std::optional {true});

    auto emptyState = withTrailer(blob(), bytesOf(emptyTrailer));
    auto empty = stripJucePrivateData(emptyState);
    check(empty.pluginState == bytesOf(apvtsBlob));
    check(!empty.bypass.has_value());

    // An older JUCE wrote none: the bytes are the plugin's.
    auto none = stripJucePrivateData(bytesOf(apvtsBlob));
    check(none.pluginState == bytesOf(apvtsBlob));
    check(!none.bypass.has_value());

    // A size that does not fit is no trailer.
    auto corrupt = std::string(16, '\0') + "JUCEPrivateData";
    corrupt[8] = 100;
    check(stripJucePrivateData(corrupt).pluginState == corrupt);

    // A private tree with no Bypass, or none readable, reads as off.
    auto noBypass = std::string(8, '\0') + std::string("T\0\0\0", 4)
                    + std::string("\x04\0\0\0\0\0\0\0", 8) + "JUCEPrivateData";
    auto stripped = stripJucePrivateData(noBypass);
    check(stripped.pluginState.empty());
    check(stripped.bypass == std::optional {false});
};

auto tChunkBank = test("JuceCompat/aVst2ChunkBankIsRead") = []
{
    auto state = withTrailer(blob(), bytesOf(bypassedTrailer));
    auto bytes = chunkBank(state, false);

    auto bank = readVst2Bank(bytes);
    check(bank.has_value());
    check(bank->bypass == std::optional {false});
    check(bank->currentProgram == 3);
    check(bank->fxVersion == 0x10203);
    check(bank->fxUniqueId == 0x46787472);
    check(bank->chunk == state);
    check(bank->programs.empty());

    // No 'VstW' header, and a version 0 bank with no current program.
    auto plainBytes = chunkBank(state, std::nullopt, 0, 0);
    auto plain = readVst2Bank(plainBytes);
    check(plain.has_value());
    check(!plain->bypass.has_value());
    check(plain->chunk == state);

    check(!readVst2Bank(chunkBank("", true)).has_value());
    check(!readVst2Bank(bytes.substr(0, bytes.size() - 1)).has_value());
    check(!readVst2Bank(blob()).has_value());
    check(!readVst2Bank("").has_value());
};

auto tValuesBank = test("JuceCompat/aVst2BankOfValuesIsRead") = []
{
    auto bank = readVst2Bank(valuesBank());
    check(bank.has_value());
    check(bank->chunk.empty());
    check(bank->currentProgram == 1);
    check(bank->programs.size() == 2);
    check(bank->programs[1].name == "Bright");
    check(bank->programs[1].values.size() == 3);
    check(bank->programs[1].values[2] == 1.5f);

    // tryVst2StateLoad refuses a bank of values with no programs.
    auto none = valuesBank();
    none[27] = 0;
    check(!readVst2Bank(none).has_value());
};

auto tVST3State = test("JuceCompat/aJuceVST3StateComesApart") = []
{
    auto state = withTrailer(blob(), bytesOf(bypassedTrailer));

    auto direct = juceVST3State(state);
    check(direct.origin == ForeignOrigin::JuceVST3);
    check(direct.bytes == bytesOf(apvtsBlob));
    check(direct.bypass == std::optional {true});
    check(!direct.program.has_value());
    check(direct.context == StateContext::Session);

    // The bank's chunk carries the trailer too; its bypass wins over 'VstW's.
    auto bank = chunkBank(state, false);
    auto replacing = juceVST3State(bank, StateContext::Preset);
    check(replacing.origin == ForeignOrigin::JuceVST2Bank);
    check(replacing.bytes == bytesOf(apvtsBlob));
    check(replacing.bypass == std::optional {true});
    check(replacing.program == std::optional {3});
    check(replacing.context == StateContext::Preset);

    auto headerBank = chunkBank(withTrailer(blob(), bytesOf(emptyTrailer)), true);
    auto headerOnly = juceVST3State(headerBank);
    check(headerOnly.bypass == std::optional {true});

    auto values = valuesBank();
    auto ofValues = juceVST3State(values);
    check(ofValues.origin == ForeignOrigin::JuceVST2Bank);
    check(ofValues.bytes == values);
};

auto tBase64 = test("JuceCompat/juceBase64Decodes") = []
{
    check(fromJuceBase64(apvtsBase64) == std::optional {blob()});

    // MemoryBlock::toBase64Encoding of 7 × FF 00 7F 80 01.
    auto odd = std::string {};

    for (auto i = 0; i < 7; ++i)
        odd += std::string("\xff\x00\x7f\x80\x01", 5);

    check(fromJuceBase64("35.+Cve.Fv+.7GfA7O.+AX.+Cve.Fv+.7GfA7O.+AX.+Cve.F.")
          == odd);

    check(fromJuceBase64("0.") == std::optional {std::string {}});
    check(!fromJuceBase64("no dot").has_value());
    check(!fromJuceBase64("-3.abc").has_value());
    check(!fromJuceBase64("99999.ab").has_value());
};

auto tStandalone = test("JuceCompat/aStandaloneSettingsFileGivesItsState") = []
{
    // PropertiesFile::saveAsXml's shape, with the audio setup as a child.
    auto settings =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n\r\n<PROPERTIES>\r\n"
        "  <VALUE name=\"audioSetup\">\r\n"
        "    <DEVICESETUP deviceType=\"CoreAudio\"/>\r\n  </VALUE>\r\n"
        "  <VALUE name=\"filterState\" val=\""
        + std::string {apvtsBase64}
        + "\"/>\r\n  <VALUE name=\"shouldMuteInput\" val=\"1\"/>\r\n"
          "</PROPERTIES>\r\n";

    check(juceSettingsValue(settings, "shouldMuteInput")
          == std::optional {std::string {"1"}});
    check(!juceSettingsValue(settings, "missing").has_value());
    check(juceStandaloneState(settings) == std::optional {blob()});

    check(!juceStandaloneState("<PROPERTIES/>").has_value());
    check(!juceStandaloneState(
               "<OTHER><VALUE name=\"filterState\" val=\"1.A\"/></OTHER>")
               .has_value());
};

auto tApvtsBlob = test("JuceCompat/anApvtsBlobSetsPlainValues") = []
{
    auto params = FixtureParams {};
    params.extra.setValue(0.9f);

    auto root =
        loadJuceApvtsBlob(bytesOf(apvtsBlob), params, {.rootType = "FixturePlugin"});
    check(root.has_value());
    check(*root->property("version") == "3");
    check(holdsFixtureValues(params));

    // JUCE keeps a parameter the state does not mention.
    check(near(params.extra.get(), 0.9f));
};

auto tApvtsValueTree = test("JuceCompat/aBinaryApvtsStateSetsPlainValues") = []
{
    auto params = FixtureParams {};
    check(loadJuceApvtsBlob(bytesOf(apvtsValueTree), params).has_value());
    check(holdsFixtureValues(params));
};

auto tApvtsMissing = test("JuceCompat/missingParametersFollowThePolicy") = []
{
    auto params = FixtureParams {};
    params.extra.setValue(0.9f);

    check(loadJuceApvtsBlob(
              bytesOf(apvtsBlob), params, {.missing = MissingPolicy::Default})
              .has_value());
    check(holdsFixtureValues(params));
    check(near(params.extra.get(), 0.3f));
};

auto tApvtsRoot = test("JuceCompat/anotherRootTypeTouchesNothing") = []
{
    auto params = FixtureParams {};
    check(!loadJuceApvtsBlob(bytesOf(apvtsBlob), params, {.rootType = "Other"})
               .has_value());
    check(near(params.cutoff.get(), 1000.f));

    check(!loadJuceApvtsBlob("{\"params\":{}}", params).has_value());
    check(!loadJuceApvtsBlob("", params).has_value());
    check(near(params.cutoff.get(), 1000.f));
};

auto tApvtsMap = test("JuceCompat/aMapNamesTheJuceIds") = []
{
    auto params = FixtureParams {};
    auto paths = std::vector<std::string> {};

    auto options = ApvtsOptions {
        .mapId = [&](const Parameter& param, std::string_view idPath)
        {
            paths.emplace_back(idPath);
            return param.id() == "voices" ? std::string {"attack"} : param.id();
        }};

    check(loadJuceApvtsBlob(bytesOf(apvtsBlob), params, options).has_value());
    check(params.voices.get() == 1);
    check(near(params.env.attack.get(), 0.25f));
    check(paths.size() == 7);
    check(paths[4] == "env/attack");

    // A throwing map applies what it reached and reports false.
    auto throwing =
        ApvtsOptions {.mapId = [](const Parameter&, std::string_view) -> std::string
                      { throw std::runtime_error("no"); }};
    check(!loadApvtsTree(
        *readJuceValueTree(bytesOf(apvtsValueTree)), params, throwing));
};

auto tApvtsXmlEdges = test("JuceCompat/apvtsReadsAsSetNewStateDoes") = []
{
    auto params = FixtureParams {};
    params.voices.setValue(3.f);

    auto xml = std::string_view {
        "<S><PARAM id=\"cutoff\" value=\"100\"/><PARAM id=\"cutoff\" value=\"200\"/>"
        "<PARAM id=\"voices\"/><PARAM id=\"mode\" value=\"junk\"/>"
        "<PARAM id=\"tone\" value=\"0.1\"/><Other id=\"enabled\" value=\"0\"/></S>"};

    check(loadApvtsXml(xml, params).has_value());

    // The last PARAM of an id wins, one with no value is the default, text with
    // no number is 0, and only PARAM children count.
    check(near(params.cutoff.get(), 200.f));
    check(params.voices.get() == 8);
    check(params.mode.getIndex() == 0);
    check(near(params.tone.get(), 0.5f));
    check(params.enabled.isOn());
};

struct RetiredParams : ParameterGroup
{
    RetiredParams() { add(comp); }

    BoolParam comp {"comp", false};
};

struct PaddedParams : ParameterGroup
{
    PaddedParams()
    {
        add(gain);
        addChild("Retired", retired, {.persistent = false});
        add(padding);
    }

    FloatParam gain {"gain", 0.f, 1.f, 0.5f};
    RetiredParams retired;
    FloatParam padding {"padding", 0.f, 1.f, 0.f, {.persistent = false}};
};

auto tApvtsUnpersisted =
    test("JuceCompat/apvtsLeavesUnpersistedParametersAlone") = []
{
    auto params = PaddedParams {};
    params.retired.comp.setOn(true);
    params.padding.setValue(0.6f);

    auto xml = std::string_view {
        "<S><PARAM id=\"gain\" value=\"0.25\"/><PARAM id=\"comp\" value=\"0\"/>"
        "<PARAM id=\"padding\" value=\"0.1\"/></S>"};

    check(loadApvtsXml(xml, params).has_value());
    check(near(params.gain.get(), 0.25f));
    check(params.retired.comp.isOn());
    check(near(params.padding.get(), 0.6f));

    // Nor does a missing PARAM reset one under MissingPolicy::Default.
    auto none = std::string_view {"<S/>"};
    check(
        loadApvtsXml(none, params, {.missing = MissingPolicy::Default}).has_value());
    check(near(params.gain.get(), 0.5f));
    check(params.retired.comp.isOn());
    check(near(params.padding.get(), 0.6f));
};
} // namespace
} // namespace JuceCompatTests
