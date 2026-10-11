#pragma once

#include "../../Audio/BusLayout.h"
#include "../../Common/Common.h"
#include "../../Devices/DeviceManager.h"
#include "../../MIDI/MidiManager.h"

#include <eacp/UI/Host/ComponentHost.h>

#include <functional>
#include <optional>

namespace MakeASound::Standalone
{

struct SettingsPanelOptions
{
    bool showInput = true;
    bool showMidiInputs = true;
    bool showMidiOutput = false;

    static SettingsPanelOptions forLayout(const BusLayout& layout);
};

// The standalone's audio and MIDI settings. The panel edits a copy of the
// config and reports it; the app applies it and answers with setConfig(), so
// what is shown is what the engine settled on. Message thread only.
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

    int preferredWidth() const;
    int preferredHeight() const;

private:
    struct Content;

    OwningPointer<Content> content;
};

} // namespace MakeASound::Standalone
