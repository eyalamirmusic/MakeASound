#pragma once

#include "../../Audio/BusLayout.h"
#include "../../Common/Common.h"
#include "../../Devices/DeviceManager.h"
#include "../../MIDI/MidiManager.h"

#include <eacp/UI/Host/ComponentHost.h>
#include <eacp/UI/Widgets/ThemeKeys.h>

#include <functional>
#include <optional>
#include <string>

namespace MakeASound::Standalone
{

// The panel's own theme keys, set in the same theme as eacp's ThemeKeys.
namespace SettingsPanelKeys
{
// A check list's frame (SettingsPanelOptions::ChoiceStyle::checkList). Unset,
// ThemeKeys::textEditorCorner's.
inline constexpr eacp::UI::NumberKey listCorner {"settingsPanel.listCorner"};
} // namespace SettingsPanelKeys

// What the rows are called, each the app's to word: a caption style of its
// own ("Output:"), its own terms, or another language.
struct SettingsPanelCaptions
{
    std::string output = "Output";
    std::string outputChannels = "Output channels";
    std::string input = "Input";
    std::string inputChannels = "Input channels";
    std::string sampleRate = "Sample rate";
    std::string blockSize = "Block size";
    std::string midiInputs = "MIDI inputs";
    std::string midiOutput = "MIDI output";

    // Where the MIDI inputs would be when there are none; empty shows nothing.
    std::string noMidiInputs = "None found";
};

// The panel's geometry, in points. A row is rowHeight tall with its control
// centred in it at controlHeight; a list is as tall as its rows, and its
// caption sits beside the first of them. The caption column is captionWidth
// wide, captionGap short of the controls, which fill the rest.
struct SettingsPanelMetrics
{
    float width = 420.f;
    float horizontalPadding = 12.f;
    float verticalPadding = 12.f;
    float rowHeight = 32.f;
    float controlHeight = 26.f;
    float captionWidth = 120.f;
    float captionGap = 0.f;
    eacp::UI::Justification captionJustification = eacp::UI::Justification::Left;

    // Space added between the devices and channels, the sample rate and block
    // size, and the MIDI rows.
    float sectionGap = 0.f;

    // A check list's rows (ChoiceStyle::checkList), and the fewest it is tall
    // enough for, whatever it holds.
    float listRowHeight = 22.f;
    int listMinimumRows = 2;

    // The face's size, unless a theme set on the panel holds a face.
    float fontSize = 13.f;

    // Rows tall enough for a finger and a larger font: the iOS app's.
    static SettingsPanelMetrics forTouch();
};

struct SettingsPanelOptions
{
    // How a set of choices is shown.
    enum class ChoiceStyle
    {
        // Channels in a combo box; MIDI inputs a checkbox row each.
        standard,

        // A framed list with a checkbox per item. For channels, the one ticked
        // is the selection, and ticking another moves it there.
        checkList
    };

    bool showInput = true;
    bool showMidiInputs = true;
    bool showMidiOutput = false;

    // forTouch()'s metrics, unless metrics says otherwise.
    bool touch = false;

    // Unset: forTouch() with touch, else SettingsPanelMetrics's defaults.
    std::optional<SettingsPanelMetrics> metrics;

    SettingsPanelCaptions captions;

    ChoiceStyle channelStyle = ChoiceStyle::standard;
    ChoiceStyle midiInputStyle = ChoiceStyle::standard;

    // Offer only stereo pairs, and a last odd channel on its own, rather than
    // every channel and every pair.
    bool stereoPairsOnly = false;

    // Output then input device, then their channels, rather than each device
    // followed by its channels.
    bool devicesBeforeChannels = false;

    // Leave the input channels' row out while the input device is None, rather
    // than showing it empty.
    bool hideChannelsWithoutDevice = false;

    // How a choice is labelled. Unset: "1", "1/2"; and the size alone, "512".
    std::function<std::string(int firstChannel, int count)> channelLabel;
    std::function<std::string(int blockSize, int sampleRate)> blockSizeLabel;

    // A button at the end of the output device's row, calling
    // SettingsPanel::onTestOutput -- StandaloneHost::playTestTone, say. Empty,
    // no button.
    std::string testButtonLabel;

    static SettingsPanelOptions forLayout(const BusLayout& layout);

    SettingsPanelMetrics resolvedMetrics() const;
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
//
// What the rows say and where they go is the options': captions, metrics,
// combo boxes or check lists, labels. Left as they are, the panel looks as it
// always has.
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

    // The test button (SettingsPanelOptions::testButtonLabel) was pressed.
    std::function<void()> onTestOutput;

    // Re-reads devices and ports and rebuilds every list, firing no callback.
    // A 2 Hz timer of the panel's own calls it when either list changed.
    void refresh();

    // A full-width button below the rows, in the order added: the iOS app's
    // resets, which a desktop app keeps in its menu bar.
    void addAction(std::string label, std::function<void()> onClick);

    int preferredWidth() const;
    int preferredHeight() const;

    const SettingsPanelOptions& getOptions() const;
    const SettingsPanelMetrics& getMetrics() const;

private:
    struct Content;

    OwningPointer<Content> content;
};

} // namespace MakeASound::Standalone
