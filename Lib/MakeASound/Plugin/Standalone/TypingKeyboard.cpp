#include "TypingKeyboard.h"

#include <eacp/Graphics/Graphics/Keyboard.h>

#include <algorithm>

namespace MakeASound::Standalone
{

namespace
{
namespace Key = eacp::Graphics::KeyCode;

constexpr auto lowestBase = 0;
constexpr auto highestBase = 127 - TypingKeyboard::highestSemitone;

constexpr auto noteKeys = std::array<uint16_t, 17> {Key::A,
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
} // namespace

TypingKeyboard::TypingKeyboard(Sink sinkToUse)
    : sink(std::move(sinkToUse))
{
    held.fill(-1);
}

std::optional<int> TypingKeyboard::semitoneForKey(uint16_t keyCode) noexcept
{
    auto found = std::ranges::find(noteKeys, keyCode);

    if (found == noteKeys.end())
        return std::nullopt;

    return static_cast<int>(found - noteKeys.begin());
}

bool TypingKeyboard::keyDown(uint16_t keyCode, bool isRepeat, bool isChord)
{
    if (isRepeat || isChord)
        return false;

    if (keyCode == Key::Z || keyCode == Key::X)
    {
        auto shift = keyCode == Key::Z ? -12 : 12;
        base = std::clamp(base + shift, lowestBase, highestBase);
        return true;
    }

    auto semitone = semitoneForKey(keyCode);

    if (!semitone || held[keyCode] >= 0)
        return false;

    auto note = base + *semitone;

    if (!sink(MIDI::Event::noteOn(0, note, velocity)))
        return false;

    held[keyCode] = note;
    return true;
}

bool TypingKeyboard::keyUp(uint16_t keyCode)
{
    if (keyCode == Key::Z || keyCode == Key::X)
        return true;

    if (!semitoneForKey(keyCode))
        return false;

    release(keyCode);
    return true;
}

void TypingKeyboard::allNotesOff()
{
    for (auto key: noteKeys)
        release(key);
}

void TypingKeyboard::release(uint16_t keyCode)
{
    auto& note = held[keyCode];

    if (note < 0)
        return;

    if (sink(MIDI::Event::noteOff(0, note, 0.f)))
        note = -1;
}

} // namespace MakeASound::Standalone
