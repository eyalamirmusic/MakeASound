#pragma once

#include "../../MIDI/MIDI.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace MakeASound::Standalone
{

// Ableton's computer keyboard: A W S E D F T G Y H U J K O L P ; play the
// semitones 0..16 above the base note, Z and X move it down and up an octave.
// Key codes follow eacp's KeyCode (macOS virtual key codes on every platform).
class TypingKeyboard
{
public:
    // False when the event went nowhere: the key is then not counted as held, so
    // no note-off is owed for a note-on that was dropped, and the reverse.
    using Sink = std::function<bool(const MIDI::Event&)>;

    explicit TypingKeyboard(Sink sinkToUse);

    // True when the key played, released or shifted something. A repeat, a chord
    // (command, control or alt held) or a key already down is ignored on the way
    // down; a key-up is never filtered.
    bool keyDown(uint16_t keyCode, bool isRepeat, bool isChord);
    bool keyUp(uint16_t keyCode);

    // A note-off for every held note.
    void allNotesOff();

    int baseNote() const noexcept { return base; }

    // 0..16, or nullopt for a key that plays nothing.
    static std::optional<int> semitoneForKey(uint16_t keyCode) noexcept;

    static constexpr int defaultBaseNote = 60;
    static constexpr int highestSemitone = 16;
    static constexpr float velocity = 0.8f;

private:
    void release(uint16_t keyCode);

    Sink sink;
    int base = defaultBaseNote;

    // The note each key is holding, so a release after an octave shift ends the
    // note that was played. -1 for none.
    std::array<int, 128> held {};
};

} // namespace MakeASound::Standalone
