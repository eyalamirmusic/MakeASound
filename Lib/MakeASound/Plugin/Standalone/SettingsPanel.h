#pragma once

#include "../../Audio/BusLayout.h"
#include "../../Common/Common.h"
#include "../../Devices/DeviceManager.h"
#include "../../MIDI/MidiManager.h"

#include <eacp/UI/Host/ComponentHost.h>

#include <functional>
#include <optional>
#include <string>

namespace MakeASound::Standalone
{

struct SettingsPanelOptions
{
    bool showInput = true;
    bool showMidiInputs = true;
    bool showMidiOutput = false;

    // Rows tall enough for a finger and a larger font: the iOS app's.
    bool touch = false;

    static SettingsPanelOptions forLayout(const BusLayout& layout);
};

// The standalone's audio and MIDI settings. The panel edits a copy of the
// config and reports it; the app applies it and answers with setConfig(), so
// what is shown is what the engine settled on. Message thread only.
//
// Drawn in its own theme, as any host's tree is: an app that dresses its
// settings like the rest of its product calls setTheme with its keys and
// values -- eacp's ThemeKeys, a face under ThemeKeys::font included -- and
// the panel reads nothing else. The captions are ThemeKeys::dimText. Unset,
// eacp's defaultTheme() at the rows' own size (13 points, 17 with touch).
class SettingsPanel final : public eacp::UI::ComponentHost
{
public:
    SettingsPanel(DeviceManager& devicesToUse,
                  MidiManager& midiToUse,
                  SettingsPanelOptions optionsToUse);
    ~SettingsPanel() override;

    void setConfig(const StreamConfig& config);
    const StreamConfig& getConfig() const;

    // The manager cannot say which output is open, so the app tells the panel
    // the one it restored.
    void setMidiOutput(std::optional<int> portId);
    std::optional<int> getMidiOutput() const;

    std::function<void(const StreamConfig&)> onConfigChanged;
    std::function<void(int portId, bool open)> onMidiInputToggled;
    std::function<void(std::optional<int> portId)> onMidiOutputChanged;

    // Re-reads devices and ports and rebuilds every list, firing no callback.
    // A 2 Hz timer of the panel's own calls it when either list changed.
    void refresh();

    // A full-width button below the rows, in the order added: the iOS app's
    // resets, which a desktop app keeps in its menu bar.
    void addAction(std::string label, std::function<void()> onClick);

    int preferredWidth() const;
    int preferredHeight() const;

private:
    struct Content;

    OwningPointer<Content> content;
};

} // namespace MakeASound::Standalone
