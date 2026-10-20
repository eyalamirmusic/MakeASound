// The default app's pieces that need eacp's window tier but no window: the
// typing keyboard's mapping and the editor window's options.

#include "TestEditor.h"

#include <MakeASound/Plugin/Standalone/TypingKeyboard.h>
#include <MakeASound/Plugin/Standalone/EditorWindow.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Graphics/Graphics/Keyboard.h>

#include <array>
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
} // namespace
} // namespace StandaloneUITests
