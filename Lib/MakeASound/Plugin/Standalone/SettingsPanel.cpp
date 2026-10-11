#include "SettingsPanel.h"
#include "../../Devices/DeviceQueries.h"
#include "../../UI/Dropdown.h"

#include <eacp/Core/Core.h>
#include <eacp/UI/UI.h>

#include <algorithm>

namespace MakeASound::Standalone
{
namespace Widgets = eacp::UI;

namespace
{
constexpr auto padding = 12.f;
constexpr auto rowHeight = 32.f;
constexpr auto controlHeight = 26.f;
constexpr auto captionWidth = 120.f;
constexpr auto panelWidth = 420;
constexpr auto pollHz = 2;
constexpr auto noneId = -1;

// A ComboBox over a DropdownInfo: items in order, and the ids beside them so a
// selected index maps back to what it names.
struct Choice
{
    void fill(const UI::DropdownInfo& info)
    {
        box.clear();
        ids.clear();

        auto selected = -1;

        for (const auto& item: info.items)
        {
            if (item.id == info.currentId)
                selected = ids.size();

            ids.add(item.id);
            box.addItem(item.label);
        }

        box.setSelectedIndex(selected);
    }

    std::optional<int> idAt(int index) const
    {
        if (index < 0 || index >= ids.size())
            return std::nullopt;

        return ids[index];
    }

    Widgets::ComboBox box;
    Vector<int> ids;
};

UI::DropdownInfo withNone(const UI::DropdownInfo& info)
{
    auto result = UI::DropdownInfo {};
    result.currentId = info.currentId;
    result.items.create(noneId, "None");

    for (const auto& item: info.items)
        result.items.add(item);

    return result;
}

bool sameDevices(const Vector<DeviceInfo>& a, const Vector<DeviceInfo>& b)
{
    auto same = [](const DeviceInfo& x, const DeviceInfo& y)
    {
        return x.id == y.id && x.name == y.name
               && x.outputChannels == y.outputChannels
               && x.inputChannels == y.inputChannels
               && std::ranges::equal(x.sampleRates, y.sampleRates);
    };

    return std::ranges::equal(a, b, same);
}

struct Snapshot
{
    bool sameAs(const Snapshot& other) const
    {
        return sameDevices(devices, other.devices)
               && std::ranges::equal(midiInputs, other.midiInputs)
               && std::ranges::equal(midiOutputs, other.midiOutputs);
    }

    Vector<DeviceInfo> devices;
    Vector<MidiPortInfo> midiInputs;
    Vector<MidiPortInfo> midiOutputs;
};

// The rows, laid out at their natural height inside the scroll panel.
struct Form final : Widgets::Component
{
    explicit Form(SettingsPanelOptions optionsToUse)
        : options(optionsToUse)
    {
        for (auto* label: {&outputCaption,
                           &outputChannelsCaption,
                           &inputCaption,
                           &inputChannelsCaption,
                           &rateCaption,
                           &blockCaption,
                           &midiInputsCaption,
                           &midiOutputCaption,
                           &noMidiInputs})
            label->setColour(Widgets::defaultTheme().dimText);

        addChildren({outputCaption, output.box, outputChannelsCaption});
        addChildren({outputChannels.box, rateCaption, rate.box});
        addChildren({blockCaption, block.box});

        if (options.showInput)
            addChildren(
                {inputCaption, input.box, inputChannelsCaption, inputChannels.box});

        if (options.showMidiInputs)
            addChildren({midiInputsCaption, noMidiInputs});

        if (options.showMidiOutput)
            addChildren({midiOutputCaption, midiOutput.box});

        updateHeight();
    }

    int rowCount() const
    {
        auto rows =
            4 + (options.showInput ? 2 : 0) + (options.showMidiOutput ? 1 : 0);

        if (options.showMidiInputs)
            rows += std::max(midiInputs.size(), 1);

        return rows;
    }

    float naturalHeight() const { return rowCount() * rowHeight + 2.f * padding; }

    void updateHeight() { setBounds(getBounds().withHeight(naturalHeight())); }

    void resized() override
    {
        auto area = getLocalBounds().inset(padding);

        auto row = [&](Widgets::Label* caption, Widgets::Component& control)
        {
            auto line = area.removeFromTop(rowHeight);
            auto inset = (line.h - controlHeight) * 0.5f;
            auto left = line.removeFromLeft(captionWidth);

            if (caption != nullptr)
                caption->setBounds(left.inset(0.f, inset));

            control.setBounds(line.inset(0.f, inset));
        };

        row(&outputCaption, output.box);
        row(&outputChannelsCaption, outputChannels.box);

        if (options.showInput)
        {
            row(&inputCaption, input.box);
            row(&inputChannelsCaption, inputChannels.box);
        }

        row(&rateCaption, rate.box);
        row(&blockCaption, block.box);

        if (options.showMidiInputs)
        {
            if (midiInputs.empty())
                row(&midiInputsCaption, noMidiInputs);

            for (auto i = 0; i < midiInputs.size(); ++i)
                row(i == 0 ? &midiInputsCaption : nullptr, *midiInputs[i]);
        }

        if (options.showMidiOutput)
            row(&midiOutputCaption, midiOutput.box);
    }

    SettingsPanelOptions options;

    Widgets::Label outputCaption {"Output"};
    Widgets::Label outputChannelsCaption {"Output channels"};
    Widgets::Label inputCaption {"Input"};
    Widgets::Label inputChannelsCaption {"Input channels"};
    Widgets::Label rateCaption {"Sample rate"};
    Widgets::Label blockCaption {"Block size"};
    Widgets::Label midiInputsCaption {"MIDI inputs"};
    Widgets::Label midiOutputCaption {"MIDI output"};
    Widgets::Label noMidiInputs {"None found"};

    Choice output;
    Choice outputChannels;
    Choice input;
    Choice inputChannels;
    Choice rate;
    Choice block;
    Choice midiOutput;

    Vector<OwningPointer<Widgets::Checkbox>> midiInputs;
};
} // namespace

SettingsPanelOptions SettingsPanelOptions::forLayout(const BusLayout& layout)
{
    return {.showInput = !layout.inputs.empty(),
            .showMidiInputs = !layout.midiInputs.empty(),
            .showMidiOutput = !layout.midiOutputs.empty()};
}

struct SettingsPanel::Content final : Widgets::Component
{
    Content(SettingsPanel& ownerToUse,
            DeviceManager& devicesToUse,
            MidiManager& midiToUse,
            SettingsPanelOptions options)
        : owner(ownerToUse)
        , devices(devicesToUse)
        , midi(midiToUse)
        , form(options)
    {
        wire();
        scroll.setContent(form);
        addAndMakeVisible(scroll);
    }

    void paint(Widgets::Graphics& g) override
    {
        g.fillAll(Widgets::defaultTheme().background);
    }

    void resized() override { scroll.setBounds(getLocalBounds()); }

    Snapshot takeSnapshot() const
    {
        auto result = Snapshot {};
        result.devices = devices.getDevices();

        if (form.options.showMidiInputs)
            result.midiInputs = midi.getInputPorts();

        if (form.options.showMidiOutput)
            result.midiOutputs = midi.getOutputPorts();

        return result;
    }

    void refresh()
    {
        snapshot = takeSnapshot();
        rebuild();
    }

    void poll()
    {
        auto latest = takeSnapshot();

        if (latest.sameAs(snapshot))
            return;

        snapshot = std::move(latest);
        rebuild();
    }

    const DeviceInfo* findDevice(int id) const
    {
        for (const auto& device: snapshot.devices)
            if (device.id == id)
                return &device;

        return nullptr;
    }

    // The current device by id from the latest enumeration, else as configured.
    DeviceInfo currentDevice(const std::optional<StreamParameters>& side) const
    {
        if (!side)
            return {};

        if (const auto* device = findDevice(side->device.id))
            return *device;

        return side->device;
    }

    void rebuild()
    {
        auto out = currentDevice(config.output);
        auto in = currentDevice(config.input);
        auto outId = config.output ? config.output->device.id : noneId;
        auto inId = config.input ? config.input->device.id : noneId;

        form.output.fill(UI::makeOutputDeviceDropdown(snapshot.devices, outId));
        form.outputChannels.fill(
            config.output
                ? UI::makeOutputChannelDropdown(
                      out, config.output->firstChannel, config.output->nChannels)
                : UI::DropdownInfo {});

        if (form.options.showInput)
        {
            form.input.fill(
                withNone(UI::makeInputDeviceDropdown(snapshot.devices, inId)));
            form.inputChannels.fill(
                config.input
                    ? UI::makeInputChannelDropdown(
                          in, config.input->firstChannel, config.input->nChannels)
                    : UI::DropdownInfo {});
        }

        const auto& clocked = config.output ? out : in;

        form.rate.fill(UI::makeSampleRateDropdown(clocked, config.sampleRate));
        form.block.fill(UI::makeBlockSizeDropdown(getSupportedBlockSizes(clocked),
                                                  config.maxBlockSize));

        if (form.options.showMidiInputs)
            rebuildMidiInputs();

        if (form.options.showMidiOutput)
            rebuildMidiOutput();
    }

    // Recreated only when the ports changed, since a checkbox may be the one
    // whose onChange is running.
    void rebuildMidiInputs()
    {
        const auto& ports = snapshot.midiInputs;

        if (!std::ranges::equal(ports, shownMidiInputs))
        {
            form.midiInputs.clear();
            shownMidiInputs = ports;

            for (const auto& port: ports)
            {
                auto& box = *form.midiInputs.add(
                    EA::makeOwned<Widgets::Checkbox>(port.name));
                auto id = port.id;

                box.onChange = [this, id](bool on)
                {
                    if (owner.onMidiInputToggled)
                        owner.onMidiInputToggled(id, on);

                    showOpenMidiInputs();
                };

                form.addAndMakeVisible(box);
            }

            form.noMidiInputs.setVisible(ports.empty());
            form.updateHeight();
            form.resized();
            scroll.resized();
        }

        showOpenMidiInputs();
    }

    void showOpenMidiInputs()
    {
        for (auto i = 0; i < form.midiInputs.size(); ++i)
            form.midiInputs[i]->setChecked(midi.isInputOpen(shownMidiInputs[i].id));
    }

    void rebuildMidiOutput()
    {
        auto info = UI::DropdownInfo {};
        info.currentId = midiOutput.value_or(noneId);

        for (const auto& port: snapshot.midiOutputs)
            info.items.create(port.id, port.name);

        form.midiOutput.fill(withNone(info));
    }

    void reconcileSampleRate()
    {
        auto out = currentDevice(config.output);
        auto in = currentDevice(config.input);

        auto supports = [this](const DeviceInfo& device)
        {
            return device.sampleRates.empty()
                   || deviceSupportsSampleRate(device, config.sampleRate);
        };

        if (config.sampleRate <= 0 || !supports(out) || !supports(in))
            config.sampleRate = pickCompatibleSampleRate(out, in);
    }

    void changed()
    {
        rebuild();

        if (owner.onConfigChanged)
            owner.onConfigChanged(config);
    }

    void wire()
    {
        form.output.box.onChange = [this](int index)
        {
            auto id = form.output.idAt(index);
            const auto* device = id ? findDevice(*id) : nullptr;

            if (device == nullptr || device->outputChannels == 0)
                return;

            config.output = StreamParameters {*device, false};
            reconcileSampleRate();
            changed();
        };

        form.input.box.onChange = [this](int index)
        {
            auto id = form.input.idAt(index);

            if (!id)
                return;

            if (*id == noneId)
            {
                config.input.reset();
            }
            else
            {
                const auto* device = findDevice(*id);

                if (device == nullptr || device->inputChannels == 0)
                    return;

                config.input = StreamParameters {*device, true};
                reconcileSampleRate();
            }

            changed();
        };

        auto channels = [this](Choice& choice, std::optional<StreamParameters>& side)
        {
            choice.box.onChange = [this, &choice, &side](int index)
            {
                auto id = choice.idAt(index);

                if (!id || !side)
                    return;

                auto selection = UI::decodeChannelSelection(*id);
                side->firstChannel = selection.firstChannel;
                side->nChannels = selection.count;
                changed();
            };
        };

        channels(form.outputChannels, config.output);
        channels(form.inputChannels, config.input);

        form.rate.box.onChange = [this](int index)
        {
            if (auto id = form.rate.idAt(index))
            {
                config.sampleRate = *id;
                changed();
            }
        };

        form.block.box.onChange = [this](int index)
        {
            if (auto id = form.block.idAt(index))
            {
                config.maxBlockSize = *id;
                changed();
            }
        };

        form.midiOutput.box.onChange = [this](int index)
        {
            auto id = form.midiOutput.idAt(index);

            if (!id)
                return;

            midiOutput = *id == noneId ? std::nullopt : id;

            if (owner.onMidiOutputChanged)
                owner.onMidiOutputChanged(midiOutput);
        };
    }

    SettingsPanel& owner;
    DeviceManager& devices;
    MidiManager& midi;

    StreamConfig config;
    std::optional<int> midiOutput;
    Snapshot snapshot;
    Vector<MidiPortInfo> shownMidiInputs;

    Widgets::ScrollPanel scroll;
    Form form;

    eacp::Threads::Timer timer {[this] { poll(); }, pollHz};
};

SettingsPanel::SettingsPanel(DeviceManager& devicesToUse,
                             MidiManager& midiToUse,
                             SettingsPanelOptions optionsToUse)
    : content(EA::makeOwned<Content>(*this, devicesToUse, midiToUse, optionsToUse))
{
    setFontPointSize(13.f);
    setBackgroundColour(Widgets::defaultTheme().background);
    setRootComponent(*content);

    content->refresh();
}

SettingsPanel::~SettingsPanel() = default;

void SettingsPanel::setConfig(const StreamConfig& config)
{
    content->config = config;
    content->refresh();
}

const StreamConfig& SettingsPanel::getConfig() const
{
    return content->config;
}

void SettingsPanel::setMidiOutput(std::optional<int> portId)
{
    content->midiOutput = portId;

    if (content->form.options.showMidiOutput)
        content->rebuildMidiOutput();
}

std::optional<int> SettingsPanel::getMidiOutput() const
{
    return content->midiOutput;
}

void SettingsPanel::refresh()
{
    content->refresh();
}

int SettingsPanel::preferredWidth() const
{
    return panelWidth;
}

int SettingsPanel::preferredHeight() const
{
    return static_cast<int>(content->form.naturalHeight());
}

} // namespace MakeASound::Standalone
