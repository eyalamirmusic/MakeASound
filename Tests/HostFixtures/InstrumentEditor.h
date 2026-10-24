#pragma once

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace HostFixtures
{

// HostFixtureInstrument's editor: fixed-size (not resizable) at initialSize, and
// once attached, on the next turn of the message loop, it asks for resizedSize
// itself, as a fixed-size GUI whose own zoom menu or tab change resizes it.
// InstrumentEditor.cpp where eacp graphics is built, NoInstrumentEditor.cpp
// (null, the format's generic editor) elsewhere.
constexpr auto initialSize = MakeASound::EditorSize {320, 200};
constexpr auto resizedSize = MakeASound::EditorSize {400, 250};

MakeASound::OwningPointer<MakeASound::Editor> createInstrumentEditor();

} // namespace HostFixtures
