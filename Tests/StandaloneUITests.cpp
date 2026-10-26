// The default app's pieces that need eacp's window tier but no window: the
// typing keyboard's mapping, the editor window's options and the settings
// panel's look.

#include "TestEditor.h"

#include <MakeASound/Plugin/Standalone/TypingKeyboard.h>
#include <MakeASound/Plugin/Standalone/EditorWindow.h>
#include <MakeASound/Plugin/Standalone/SettingsPanel.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Graphics/Graphics/Keyboard.h>
#include <eacp/UI/UI.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace StandaloneUITests
{
using namespace nano;
using namespace TestPlugins;
using namespace MakeASound::Standalone;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
// The typing keyboard.

namespace Key = eacp::Graphics::KeyCode;

struct Typist
{
    TypingKeyboard keyboard {[this](const MIDI::Event& event)
                             {
                                 if (accepting)
                                     events.push_back(event);

                                 return accepting;
                             }};
    std::vector<MIDI::Event> events;
    bool accepting = true;
};

bool isNoteOn(const MIDI::Event& event, int pitch)
{
    return event.isNoteOn() && event.asNoteOn()->pitch == pitch;
}

bool isNoteOff(const MIDI::Event& event, int pitch)
{
    return event.isNoteOff() && event.asNoteOff()->pitch == pitch;
}

auto tKeyMap = test("Standalone/typingKeyboardMapsEveryKey") = []
{
    auto keys = std::array<uint16_t, 17> {Key::A,
                                          Key::W,
                                          Key::S,
                                          Key::E,
                                          Key::D,
                                          Key::F,
                                          Key::T,
                                          Key::G,
                                          Key::Y,
                                          Key::H,
                                          Key::U,
                                          Key::J,
                                          Key::K,
                                          Key::O,
                                          Key::L,
                                          Key::P,
                                          Key::Semicolon};

    auto mapped = true;

    for (auto i = 0; i < static_cast<int>(keys.size()); ++i)
        mapped = mapped && TypingKeyboard::semitoneForKey(keys[i]) == i;

    check(mapped);
    check(!TypingKeyboard::semitoneForKey(Key::Q));
    check(!TypingKeyboard::semitoneForKey(Key::Z));
    check(!TypingKeyboard::semitoneForKey(Key::X));
};

auto tKeyDown = test("Standalone/typingKeyboardPlaysAtTheBaseNote") = []
{
    auto typist = Typist {};

    check(typist.keyboard.baseNote() == 60);
    check(typist.keyboard.keyDown(Key::A, false, false));
    check(typist.keyboard.keyDown(Key::Semicolon, false, false));

    check(typist.events.size() == 2);
    check(isNoteOn(typist.events[0], 60));
    check(isNoteOn(typist.events[1], 76));
    check(typist.events[0].asNoteOn()->velocity == 0.8f);
    check(typist.events[0].channel == 0);

    check(typist.keyboard.keyUp(Key::A));
    check(typist.events.size() == 3);
    check(isNoteOff(typist.events[2], 60));
};

auto tKeyIgnored = test("Standalone/typingKeyboardIgnoresRepeatsAndChords") = []
{
    auto typist = Typist {};

    check(!typist.keyboard.keyDown(Key::A, true, false));
    check(!typist.keyboard.keyDown(Key::A, false, true));
    check(!typist.keyboard.keyDown(Key::Z, false, true));
    check(typist.events.empty());
    check(typist.keyboard.baseNote() == 60);

    check(typist.keyboard.keyDown(Key::A, false, false));
    check(!typist.keyboard.keyDown(Key::A, false, false));
    check(typist.events.size() == 1);

    check(!typist.keyboard.keyDown(Key::Q, false, false));
    check(!typist.keyboard.keyUp(Key::Q));
    check(typist.events.size() == 1);
};

// The event form, which the window and the editor's unhandled keys both take.
auto tKeyEvent = test("Standalone/typingKeyboardPlaysAKeyEvent") = []
{
    auto typist = Typist {};

    auto event = eacp::Graphics::KeyEvent {};
    event.keyCode = Key::A;
    event.type = eacp::Graphics::KeyEventType::Down;
    event.modifiers.command = true;

    // A chord plays nothing.
    check(!typist.keyboard.key(event));
    check(typist.events.empty());

    event.modifiers.command = false;
    check(typist.keyboard.key(event));
    check(typist.events.size() == 1);
    check(isNoteOn(typist.events[0], 60));

    event.isRepeat = true;
    check(!typist.keyboard.key(event));

    event.type = eacp::Graphics::KeyEventType::Up;
    event.isRepeat = false;
    check(typist.keyboard.key(event));
    check(typist.events.size() == 2);
    check(isNoteOff(typist.events[1], 60));
};

auto tKeyShift = test("Standalone/typingKeyboardReleasesTheNoteItPlayed") = []
{
    auto typist = Typist {};

    typist.keyboard.keyDown(Key::D, false, false);
    check(typist.keyboard.keyDown(Key::Z, false, false));
    check(typist.keyboard.baseNote() == 48);

    typist.keyboard.keyUp(Key::D);
    typist.keyboard.keyDown(Key::D, false, false);

    check(typist.events.size() == 3);
    check(isNoteOn(typist.events[0], 64));
    check(isNoteOff(typist.events[1], 64));
    check(isNoteOn(typist.events[2], 52));
};

auto tKeyAllOff = test("Standalone/typingKeyboardAllNotesOffReleasesEachOnce") = []
{
    auto typist = Typist {};

    typist.keyboard.keyDown(Key::A, false, false);
    typist.keyboard.keyDown(Key::X, false, false);
    typist.keyboard.keyDown(Key::K, false, false);
    typist.keyboard.allNotesOff();

    check(typist.events.size() == 4);
    check(isNoteOff(typist.events[2], 60));
    check(isNoteOff(typist.events[3], 84));

    typist.keyboard.allNotesOff();
    typist.keyboard.keyUp(Key::A);
    check(typist.events.size() == 4);
};

auto tKeyClamp = test("Standalone/typingKeyboardClampsTheOctave") = []
{
    auto typist = Typist {};

    for (auto i = 0; i < 12; ++i)
        typist.keyboard.keyDown(Key::Z, false, false);

    check(typist.keyboard.baseNote() == 0);

    typist.keyboard.keyDown(Key::A, false, false);
    check(isNoteOn(typist.events.back(), 0));

    for (auto i = 0; i < 12; ++i)
        typist.keyboard.keyDown(Key::X, false, false);

    check(typist.keyboard.baseNote() == 111);

    typist.keyboard.keyDown(Key::Semicolon, false, false);
    check(isNoteOn(typist.events.back(), 127));
};

auto tDroppedNote = test("Standalone/typingKeyboardOwesNothingForADroppedNote") = []
{
    auto typist = Typist {};
    typist.accepting = false;

    check(!typist.keyboard.keyDown(Key::A, false, false));

    typist.accepting = true;
    typist.keyboard.keyUp(Key::A);
    check(typist.events.empty());

    check(typist.keyboard.keyDown(Key::A, false, false));
    typist.accepting = false;
    typist.keyboard.keyUp(Key::A);
    typist.accepting = true;
    typist.keyboard.allNotesOff();

    check(typist.events.size() == 2);
    check(isNoteOff(typist.events[1], 60));
};

// The editor window.

bool hasResizableFlag(const eacp::Graphics::WindowOptions& options)
{
    for (auto flag: options.flags)
        if (flag == eacp::Graphics::WindowFlags::Resizable)
            return true;

    return false;
}

eacp::Graphics::Point proposeSize(const eacp::Graphics::WindowOptions& options,
                                  float width,
                                  float height)
{
    return options.sizeConstraint(
        {{width, height}, eacp::Graphics::ResizeAxis::Both});
}

auto tFixedEditor = test("Standalone/aFixedEditorOpensAFixedWindow") = []
{
    auto editor = TestEditor {};
    editor.size = {500, 320};

    auto options = editorWindowOptions(editor, "Fixed");

    check(options.title == "Fixed");
    check(!hasResizableFlag(options));
    check(options.width == 500);
    check(options.height == 320);
    check(options.effectiveAllowsFullScreen());
};

auto tResizableEditor = test("Standalone/aResizableEditorConstrainsTheWindow") = []
{
    auto editor = TestEditor {};
    editor.size = {100, 2000};
    editor.limits = {
        .resizable = true, .minimum = {300, 200}, .maximum = {900, 700}};

    auto options = editorWindowOptions(editor, "Resizable");

    check(hasResizableFlag(options));
    check(options.minWidth == 300);
    check(options.minHeight == 200);
    check(options.width == 300);
    check(options.height == 700);
    check(options.effectiveAllowsFullScreen());

    for (auto [width, height]: {std::pair {50, 50},
                                std::pair {640, 480},
                                std::pair {5000, 150},
                                std::pair {1200, 1200}})
    {
        auto expected = editor.limits.constrain({width, height});
        auto answer = proposeSize(options, (float) width, (float) height);

        check(answer.x == (float) expected.width);
        check(answer.y == (float) expected.height);
    }
};

auto tRatioEditor = test("Standalone/aRatioLockedEditorKeepsItsShape") = []
{
    auto editor = TestEditor {};
    editor.size = {640, 300};
    editor.limits = {.resizable = true,
                     .minimum = {200, 100},
                     .maximum = {2000, 1000},
                     .aspectRatio = {2, 1}};

    auto options = editorWindowOptions(editor, "Ratio");

    check(!options.effectiveAllowsFullScreen());

    auto initial = options.effectiveInitialSize();
    check(initial.x == 2.f * initial.y);
    check(initial.x == (float) options.width);
    check(initial.y == (float) options.height);

    auto dragged = options.effectiveSize({900.f, 300.f});
    check(dragged.x == 2.f * dragged.y);
};

// The settings panel's look: eacp's default theme and the platform's face
// unless the app sets a theme of its own on it, as on any host.

namespace Keys = eacp::UI::ThemeKeys;

bool sameColour(const eacp::UI::Color& a, const eacp::UI::Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// A rendered pixel, eight bits a channel.
bool pixelIs(const eacp::Graphics::Image& image,
             int x,
             int y,
             const eacp::UI::Color& expected)
{
    auto actual = image.at(x, y);
    auto near = [](float a, float b) { return std::abs(a - b) <= 2.f / 255.f; };

    return near(actual.r, expected.r) && near(actual.g, expected.g)
           && near(actual.b, expected.b);
}

const auto navy = eacp::UI::Color {40.f / 255.f, 40.f / 255.f, 88.f / 255.f, 1.f};

// A look as an app gives one: values under eacp's keys, a face among them.
eacp::UI::Theme navyTheme()
{
    auto theme = eacp::UI::Theme {};
    theme.set(Keys::background, navy);
    theme.set(Keys::panel, navy);
    theme.set(Keys::dimText, {1.f, 1.f, 1.f, 1.f});
    theme.set(Keys::font,
              {.family = eacp::UI::defaultUIFontFamily(),
               .pointSize = 11.5f,
               .style = eacp::UI::FontStyle::Bold});

    return theme;
}

auto tPanelDefaultLook = test("Standalone/settingsPanelDrawsInEacpsDefaultLook") = []
{
    auto devices = MakeASound::DeviceManager {};
    auto midi = MakeASound::MidiManager {};

    auto desktop = SettingsPanel {devices, midi, {}};
    const auto& theme = eacp::UI::defaultTheme();

    // Nothing of its own: every key reads as eacp's.
    check(desktop.getTheme().isEmpty());
    check(sameColour(desktop.getTheme().get(Keys::background),
                     theme.get(Keys::background)));
    check(desktop.getFont().family == eacp::UI::defaultUIFontFamily());
    check(desktop.getFont().pointSize == 13.f);

    auto touch = SettingsPanel {devices, midi, {.touch = true}};
    check(touch.getFont().pointSize == 17.f);
};

auto tPanelGivenLook = test("Standalone/settingsPanelWearsTheThemeItIsGiven") = []
{
    auto devices = MakeASound::DeviceManager {};
    auto midi = MakeASound::MidiManager {};

    // Set after it is built, as an app that owns the panel does.
    auto panel = SettingsPanel {devices, midi, {}};
    panel.setTheme(navyTheme());

    check(sameColour(panel.getTheme().get(Keys::background), navy));
    check(sameColour(panel.getTheme().get(Keys::panel), navy));
    check(panel.getFont().pointSize == 11.5f);
    check(panel.getFont().style == eacp::UI::FontStyle::Bold);

    // Drawn in it: the panel's foot, below its last row, is the theme's
    // background rather than eacp's.
    auto width = panel.preferredWidth();
    auto height = panel.preferredHeight();
    // Off screen nothing sizes the host's tree, so it is laid out here, as
    // eacp's own host tests do.
    panel.setBounds({0.f, 0.f, (float) width, (float) height});
    panel.resized();

    auto image = panel.renderToImage(1.f);

    check(image.width() == width && image.height() == height);
    check(pixelIs(image, width / 2, height - 3, navy));
    check(pixelIs(image, 3, height / 2, navy));
};

// The rows' words, sizes and kinds: the options', and as they always were
// when left alone.

// Every component of a kind under the panel's root, depth first.
template <typename T>
std::vector<T*> findAll(eacp::UI::Component& root)
{
    auto found = std::vector<T*> {};

    auto walk = [&](auto& self, eacp::UI::Component& component) -> void
    {
        if (auto* match = dynamic_cast<T*>(&component))
            found.push_back(match);

        for (auto* child: component.getChildren())
            self(self, *child);
    };

    walk(walk, root);
    return found;
}

eacp::UI::Label* findLabel(SettingsPanel& panel, const std::string& text)
{
    for (auto* label: findAll<eacp::UI::Label>(*panel.getRootComponent()))
        if (label->getText() == text)
            return label;

    return nullptr;
}

void layOut(SettingsPanel& panel)
{
    panel.setBounds(
        {0.f, 0.f, (float) panel.preferredWidth(), (float) panel.preferredHeight()});
    panel.resized();
}

// A device no enumeration has, so the panel shows it as configured.
MakeASound::StreamConfig fourChannelConfig()
{
    auto device = MakeASound::DeviceInfo {};
    device.id = 9999;
    device.name = "Test Interface";
    device.outputChannels = 4;
    device.sampleRates.add(48000);

    auto config = MakeASound::StreamConfig {};
    config.output = MakeASound::StreamParameters {device, false, 2, 0};
    config.sampleRate = 48000;
    config.maxBlockSize = 512;

    return config;
}

auto tPanelDefaultLayout = test("Standalone/settingsPanelLaysOutAsItAlwaysHas") = []
{
    auto devices = MakeASound::DeviceManager {};
    auto midi = MakeASound::MidiManager {};

    auto metrics = SettingsPanelOptions {}.resolvedMetrics();
    check(metrics.width == 420.f && metrics.horizontalPadding == 12.f
          && metrics.verticalPadding == 12.f);
    check(metrics.rowHeight == 32.f && metrics.controlHeight == 26.f);
    check(metrics.captionWidth == 120.f && metrics.captionGap == 0.f);
    check(metrics.captionJustification == eacp::UI::Justification::Left);
    check(metrics.sectionGap == 0.f && metrics.fontSize == 13.f);

    auto touch = SettingsPanelOptions {.touch = true}.resolvedMetrics();
    check(touch.rowHeight == 56.f && touch.controlHeight == 44.f);
    check(touch.captionWidth == 140.f && touch.fontSize == 17.f);

    // A row per control, one per MIDI input or one saying there are none.
    auto panel = SettingsPanel {devices, midi, {}};
    auto midiRows = std::max(1, (int) midi.getInputPorts().size());

    check(panel.preferredWidth() == 420);
    check(panel.preferredHeight() == (6 + midiRows) * 32 + 24);

    layOut(panel);

    auto* rate = findLabel(panel, "Sample rate");
    check(rate != nullptr);

    if (rate != nullptr)
    {
        check(rate->getBounds().x == 12.f && rate->getBounds().w == 120.f);
        check(rate->getBounds().h == 26.f);
    }

    // Combo boxes, no check lists, no test button.
    check(findAll<eacp::UI::Button>(*panel.getRootComponent()).empty());
    check(findLabel(panel, "Block size") != nullptr);
};

auto tPanelHidesUnusedChannels =
    test("Standalone/settingsPanelCanLeaveOutTheChannelsOfNoInput") = []
{
    auto devices = MakeASound::DeviceManager {};
    auto midi = MakeASound::MidiManager {};

    auto shown = SettingsPanel {devices, midi, {}};
    auto hidden = SettingsPanel {devices, midi, {.hideChannelsWithoutDevice = true}};

    // No input picked: one row fewer, the caption hidden.
    shown.setConfig(fourChannelConfig());
    hidden.setConfig(fourChannelConfig());
    check(hidden.preferredHeight() == shown.preferredHeight() - 32);

    auto* caption = findLabel(hidden, "Input channels");
    check(caption != nullptr && !caption->isVisible());

    // Picking one brings the row back.
    auto config = fourChannelConfig();
    config.input = MakeASound::StreamParameters {config.output->device, true, 2, 0};
    config.input->device.inputChannels = 2;
    hidden.setConfig(config);

    check(hidden.preferredHeight() == shown.preferredHeight());
    check(caption != nullptr && caption->isVisible());
};

// A desktop toolkit's dialog: right-aligned captions with colons, closer rows
// with gaps between the sections, check lists and a test button.
SettingsPanelOptions dialogOptions()
{
    auto options = SettingsPanelOptions {.showInput = false};

    options.metrics =
        SettingsPanelMetrics {.width = 500.f,
                              .horizontalPadding = 25.f,
                              .verticalPadding = 12.f,
                              .rowHeight = 30.f,
                              .controlHeight = 24.f,
                              .captionWidth = 145.f,
                              .captionGap = 5.f,
                              .captionJustification = eacp::UI::Justification::Right,
                              .sectionGap = 12.f};

    options.captions.output = "Output:";
    options.captions.outputChannels = "Active output channels:";
    options.captions.sampleRate = "Sample rate:";
    options.captions.blockSize = "Audio buffer size:";
    options.captions.midiInputs = "Active MIDI inputs:";

    options.channelStyle = SettingsPanelOptions::ChoiceStyle::checkList;
    options.midiInputStyle = SettingsPanelOptions::ChoiceStyle::checkList;
    options.stereoPairsOnly = true;
    options.channelLabel = [](int first, int count)
    {
        return count == 2
                   ? std::to_string(first + 1) + " + " + std::to_string(first + 2)
                   : std::to_string(first + 1);
    };
    options.blockSizeLabel = [](int size, int rate)
    { return std::to_string(size) + " at " + std::to_string(rate); };
    options.testButtonLabel = "Test";

    return options;
}

auto tPanelDialogLayout = test("Standalone/settingsPanelTakesItsWordsAndSizes") = []
{
    auto devices = MakeASound::DeviceManager {};
    auto midi = MakeASound::MidiManager {};

    auto panel = SettingsPanel {devices, midi, dialogOptions()};
    panel.setConfig(fourChannelConfig());
    layOut(panel);

    check(panel.preferredWidth() == 500);

    // Rows of 30, two sections apart, and two lists at least two rows of 22
    // tall inside a one-point frame.
    auto midiRows = std::max(2, (int) midi.getInputPorts().size());
    auto channelList = 2 * 22 + 2;
    auto midiList = midiRows * 22 + 2;
    check(panel.preferredHeight()
          == 12 + 30 + (channelList + 6) + 12 + 2 * 30 + 12 + (midiList + 6) + 12);

    // Right-aligned beside the controls, which start captionGap past them.
    auto* rate = findLabel(panel, "Sample rate:");
    check(rate != nullptr);

    if (rate != nullptr)
    {
        check(rate->getBounds().x == 25.f && rate->getBounds().w == 145.f);
        check(rate->getBounds().y == 12.f + 30.f + channelList + 6.f + 12.f + 3.f);
    }

    check(findLabel(panel, "Audio buffer size:") != nullptr);
    check(findLabel(panel, "Block size") == nullptr);

    // The channels as stereo pairs in a list, the configured pair ticked.
    auto boxes = findAll<eacp::UI::Checkbox>(*panel.getRootComponent());
    auto pairs = std::vector<eacp::UI::Checkbox*> {};

    for (auto* box: boxes)
        if (box->getText().find(" + ") != std::string::npos)
            pairs.push_back(box);

    check(pairs.size() == 2);

    if (pairs.size() == 2)
    {
        check(pairs[0]->getText() == "1 + 2" && pairs[0]->isChecked());
        check(pairs[1]->getText() == "3 + 4" && !pairs[1]->isChecked());

        auto picked = std::optional<MakeASound::StreamConfig> {};
        panel.onConfigChanged = [&](const MakeASound::StreamConfig& config)
        { picked = config; };

        // Ticking the other pair moves the selection there.
        pairs[1]->setChecked(true, true);
        check(picked.has_value());

        if (picked)
            check(picked->output->firstChannel == 2
                  && picked->output->nChannels == 2);

        check(!pairs[0]->isChecked() && pairs[1]->isChecked());

        // Unticking the selection puts the tick back and changes nothing.
        picked.reset();
        pairs[1]->setChecked(false, true);
        check(!picked.has_value());
        check(pairs[1]->isChecked());
    }

    // Block sizes in the options' words, at the configured rate.
    auto combos = findAll<eacp::UI::ComboBox>(*panel.getRootComponent());
    auto labelled = false;

    for (auto* combo: combos)
        for (auto i = 0; i < combo->getNumItems(); ++i)
            labelled |= combo->getItemText(i) == "512 at 48000";

    check(labelled);

    // The test button ends the output row and says so when pressed.
    auto buttons = findAll<eacp::UI::Button>(*panel.getRootComponent());
    check(buttons.size() == 1);

    if (buttons.size() == 1)
    {
        auto tested = 0;
        panel.onTestOutput = [&] { ++tested; };
        buttons[0]->onClick();
        check(tested == 1);

        const auto& bounds = buttons[0]->getBounds();
        check(bounds.right() == 500.f - 25.f);
        check(bounds.y == 12.f + 3.f && bounds.h == 24.f);
    }
};
} // namespace
} // namespace StandaloneUITests
