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
constexpr auto pollHz = 2;
constexpr auto noneId = -1;

// A check list's frame, and how far its checkboxes sit inside it.
constexpr auto listOutline = 1.f;
constexpr auto listInset = 4.f;

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

// A framed list of checkboxes, one per item. The checkboxes are recreated
// only when the items themselves changed, since one of them may be the box
// whose onChange is running; otherwise fill() just ticks them.
class CheckList final : public Widgets::Component
{
public:
    struct Item
    {
        int id {};
        std::string label;
        bool checked {};

        bool sameEntry(const Item& other) const
        {
            return id == other.id && label == other.label;
        }
    };

    explicit CheckList(std::string emptyText)
        : empty(std::move(emptyText))
    {
        empty.setThemeColour(Widgets::ThemeKeys::dimText);
        empty.setJustification(Widgets::Justification::Centred);
        addAndMakeVisible(empty);
    }

    // True when the items changed, and so may the list's height.
    bool fill(const Vector<Item>& newItems)
    {
        auto changed = !std::ranges::equal(
            newItems, items, [](auto& a, auto& b) { return a.sameEntry(b); });

        if (changed)
            recreate(newItems);

        for (auto i = 0; i < boxes.size(); ++i)
            boxes[i]->setChecked(newItems[i].checked);

        return changed;
    }

    void setChecked(int id, bool checked)
    {
        for (auto i = 0; i < items.size(); ++i)
            if (items[i].id == id)
                boxes[i]->setChecked(checked);
    }

    float heightFor(const SettingsPanelMetrics& metrics) const
    {
        auto rows = std::max(items.size(), metrics.listMinimumRows);
        return (float) rows * metrics.listRowHeight + 2.f * listOutline;
    }

    void setRowHeight(float height) { rowHeight = height; }

    std::function<void(int id, bool checked)> onToggle;

    void paint(Widgets::Graphics& g) override
    {
        const auto& theme = getHostTheme();
        auto corner = theme.get(SettingsPanelKeys::listCorner,
                                theme.get(Widgets::ThemeKeys::textEditorCorner));
        auto bounds = getLocalBounds();

        g.setColour(theme.get(Widgets::ThemeKeys::panel));
        g.fillRoundedRect(bounds, corner);

        g.setColour(theme.get(Widgets::ThemeKeys::outline));
        g.drawRoundedRect(bounds, corner, listOutline);
    }

    void resized() override
    {
        auto area = getLocalBounds().inset(listInset, listOutline);

        empty.setBounds(area.removeFromTop(rowHeight));
        empty.setVisible(items.empty());

        area = getLocalBounds().inset(listInset, listOutline);

        for (auto& box: boxes)
            box->setBounds(area.removeFromTop(rowHeight));
    }

private:
    void recreate(const Vector<Item>& newItems)
    {
        boxes.clear();
        items = newItems;

        for (const auto& item: items)
        {
            auto& box = *boxes.add(EA::makeOwned<Widgets::Checkbox>(item.label));
            auto id = item.id;

            box.onChange = [this, id](bool checked)
            {
                if (onToggle)
                    onToggle(id, checked);
            };

            addAndMakeVisible(box);
        }

        resized();
    }

    Vector<Item> items;
    Vector<OwningPointer<Widgets::Checkbox>> boxes;
    Widgets::Label empty;
    float rowHeight = 22.f;
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

// The device's channel choices, cut to stereo pairs and labelled as the
// options ask.
UI::DropdownInfo channelChoices(const UI::DropdownInfo& all,
                                int channels,
                                const SettingsPanelOptions& options)
{
    if (!options.stereoPairsOnly && !options.channelLabel)
        return all;

    auto result = UI::DropdownInfo {};
    result.currentId = all.currentId;

    for (const auto& item: all.items)
    {
        auto selection = UI::decodeChannelSelection(item.id);
        auto lastOddChannel =
            channels % 2 == 1 && selection.firstChannel == channels - 1;

        if (options.stereoPairsOnly && selection.count != 2 && !lastOddChannel)
            continue;

        auto label =
            options.channelLabel
                ? options.channelLabel(selection.firstChannel, selection.count)
                : item.label;

        result.items.create(item.id, std::move(label));
    }

    return result;
}

UI::DropdownInfo blockSizeChoices(const Vector<int>& sizes,
                                  int currentSize,
                                  int sampleRate,
                                  const SettingsPanelOptions& options)
{
    auto info = UI::makeBlockSizeDropdown(sizes, currentSize);

    if (options.blockSizeLabel)
        for (auto& item: info.items)
            item.label = options.blockSizeLabel(item.id, sampleRate);

    return info;
}

Vector<CheckList::Item> checkItems(const UI::DropdownInfo& info)
{
    auto items = Vector<CheckList::Item> {};

    for (const auto& item: info.items)
        items.create(item.id, item.label, item.id == info.currentId);

    return items;
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

using ChoiceStyle = SettingsPanelOptions::ChoiceStyle;

// A device's channels, as a combo box or a check list.
struct ChannelPicker
{
    explicit ChannelPicker(ChoiceStyle styleToUse)
        : style(styleToUse)
    {
    }

    bool isList() const { return style == ChoiceStyle::checkList; }

    Widgets::Component& control()
    {
        if (isList())
            return list;

        return choice.box;
    }

    // True when a list's items changed, and so may its height.
    bool fill(const UI::DropdownInfo& info)
    {
        if (isList())
            return list.fill(checkItems(info));

        choice.fill(info);
        return false;
    }

    ChoiceStyle style;
    Choice choice;
    CheckList list {std::string {}};
};

// The rows, laid out at their natural height inside the scroll panel.
struct Form final : Widgets::Component
{
    explicit Form(SettingsPanelOptions optionsToUse)
        : options(std::move(optionsToUse))
        , metrics(options.resolvedMetrics())
    {
        for (auto* label: {&outputCaption,
                           &outputChannelsCaption,
                           &inputCaption,
                           &inputChannelsCaption,
                           &rateCaption,
                           &blockCaption,
                           &midiInputsCaption,
                           &midiOutputCaption})
        {
            label->setThemeColour(Widgets::ThemeKeys::dimText);
            label->setJustification(metrics.captionJustification);
        }

        noMidiInputs.setThemeColour(Widgets::ThemeKeys::dimText);

        for (auto* list: {&outputChannels.list, &inputChannels.list, &midiInputList})
            list->setRowHeight(metrics.listRowHeight);

        addChildren({outputCaption, output.box, outputChannelsCaption});
        addChildren({outputChannels.control(), rateCaption, rate.box});
        addChildren({blockCaption, block.box});

        if (hasTestButton())
            addAndMakeVisible(testButton);

        if (options.showInput)
            addChildren({inputCaption,
                         input.box,
                         inputChannelsCaption,
                         inputChannels.control()});

        if (options.showMidiInputs)
        {
            addAndMakeVisible(midiInputsCaption);

            if (midiInputsInList())
                addAndMakeVisible(midiInputList);
            else
                addAndMakeVisible(noMidiInputs);
        }

        if (options.showMidiOutput)
            addChildren({midiOutputCaption, midiOutput.box});

        updateHeight();
    }

    bool hasTestButton() const { return !options.testButtonLabel.empty(); }

    bool showsInputChannels() const
    {
        return options.showInput
               && (inputChosen || !options.hideChannelsWithoutDevice);
    }

    // Whether an input device is picked, which shows or hides its channels'
    // row under hideChannelsWithoutDevice. True when the row came or went.
    bool setInputChosen(bool chosen)
    {
        if (chosen == inputChosen)
            return false;

        inputChosen = chosen;
        inputChannelsCaption.setVisible(showsInputChannels());
        inputChannels.control().setVisible(showsInputChannels());
        return options.hideChannelsWithoutDevice;
    }

    bool midiInputsInList() const
    {
        return options.midiInputStyle == ChoiceStyle::checkList;
    }

    // Asks layOut for the height alone, which places nothing.
    float naturalHeight() const { return const_cast<Form*>(this)->layOut(false); }

    void updateHeight() { setBounds(getBounds().withHeight(naturalHeight())); }

    void resized() override { layOut(true); }

    // Walks the rows from the top, giving each its bounds when `place`, and
    // answers the height they need: the one place both sizes are decided.
    float layOut(bool place)
    {
        const auto& m = metrics;
        auto x = m.horizontalPadding;
        auto y = m.verticalPadding;
        auto gap = m.rowHeight - m.controlHeight;
        auto controlX = x + m.captionWidth + m.captionGap;
        auto controlWidth = getWidth() - m.horizontalPadding - controlX;

        auto row = [&](Widgets::Label* caption, Widgets::Component& control)
        {
            if (place)
            {
                auto top = y + gap * 0.5f;

                if (caption != nullptr)
                    caption->setBounds({x, top, m.captionWidth, m.controlHeight});

                control.setBounds({controlX, top, controlWidth, m.controlHeight});
            }

            y += m.rowHeight;
        };

        // A list is as tall as its rows, with a row's gap around it, and its
        // caption beside the first of them.
        auto listRow = [&](Widgets::Label& caption, CheckList& list)
        {
            auto height = list.heightFor(m);

            if (place)
            {
                auto top = y + gap * 0.5f;
                caption.setBounds(
                    {x, top + listOutline, m.captionWidth, m.listRowHeight});
                list.setBounds({controlX, top, controlWidth, height});
            }

            y += height + gap;
        };

        auto channelRow = [&](Widgets::Label& caption, ChannelPicker& picker)
        {
            if (picker.isList())
                listRow(caption, picker.list);
            else
                row(&caption, picker.choice.box);
        };

        row(&outputCaption, output.box);

        if (place && hasTestButton())
        {
            auto buttonWidth = measureText(options.testButtonLabel, getHostFont())
                               + m.controlHeight;
            auto bounds = output.box.getBounds();

            testButton.setBounds(bounds.removeFromRight(buttonWidth));
            bounds.removeFromRight(gap);
            output.box.setBounds(bounds);
        }

        if (options.devicesBeforeChannels)
        {
            if (options.showInput)
                row(&inputCaption, input.box);

            channelRow(outputChannelsCaption, outputChannels);

            if (showsInputChannels())
                channelRow(inputChannelsCaption, inputChannels);
        }
        else
        {
            channelRow(outputChannelsCaption, outputChannels);

            if (options.showInput)
                row(&inputCaption, input.box);

            if (showsInputChannels())
                channelRow(inputChannelsCaption, inputChannels);
        }

        y += m.sectionGap;

        row(&rateCaption, rate.box);
        row(&blockCaption, block.box);

        if (options.showMidiInputs || options.showMidiOutput)
            y += m.sectionGap;

        if (options.showMidiInputs)
        {
            if (midiInputsInList())
            {
                listRow(midiInputsCaption, midiInputList);
            }
            else
            {
                if (midiInputs.empty())
                    row(&midiInputsCaption, noMidiInputs);

                for (auto i = 0; i < midiInputs.size(); ++i)
                    row(i == 0 ? &midiInputsCaption : nullptr, *midiInputs[i]);
            }
        }

        if (options.showMidiOutput)
            row(&midiOutputCaption, midiOutput.box);

        for (auto& action: actions)
        {
            if (place)
                action->setBounds({x,
                                   y + gap * 0.5f,
                                   getWidth() - 2.f * m.horizontalPadding,
                                   m.controlHeight});

            y += m.rowHeight;
        }

        return y + m.verticalPadding;
    }

    SettingsPanelOptions options;
    SettingsPanelMetrics metrics;

    Widgets::Label outputCaption {options.captions.output};
    Widgets::Label outputChannelsCaption {options.captions.outputChannels};
    Widgets::Label inputCaption {options.captions.input};
    Widgets::Label inputChannelsCaption {options.captions.inputChannels};
    Widgets::Label rateCaption {options.captions.sampleRate};
    Widgets::Label blockCaption {options.captions.blockSize};
    Widgets::Label midiInputsCaption {options.captions.midiInputs};
    Widgets::Label midiOutputCaption {options.captions.midiOutput};
    Widgets::Label noMidiInputs {options.captions.noMidiInputs};

    Choice output;
    ChannelPicker outputChannels {options.channelStyle};
    Choice input;
    ChannelPicker inputChannels {options.channelStyle};
    Choice rate;
    Choice block;
    Choice midiOutput;

    Widgets::Button testButton {options.testButtonLabel};
    bool inputChosen = true;

    Vector<OwningPointer<Widgets::Checkbox>> midiInputs;
    CheckList midiInputList {options.captions.noMidiInputs};
    Vector<OwningPointer<Widgets::Button>> actions;
};
} // namespace

SettingsPanelMetrics SettingsPanelMetrics::forTouch()
{
    // 44 points is the smallest target Apple's guidelines give a finger.
    return {.rowHeight = 56.f,
            .controlHeight = 44.f,
            .captionWidth = 140.f,
            .listRowHeight = 44.f,
            .fontSize = 17.f};
}

SettingsPanelOptions SettingsPanelOptions::forLayout(const BusLayout& layout)
{
    return {.showInput = !layout.inputs.empty(),
            .showMidiInputs = !layout.midiInputs.empty(),
            .showMidiOutput = !layout.midiOutputs.empty()};
}

SettingsPanelMetrics SettingsPanelOptions::resolvedMetrics() const
{
    if (metrics)
        return *metrics;

    return touch ? SettingsPanelMetrics::forTouch() : SettingsPanelMetrics {};
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
        , form(std::move(options))
    {
        wire();
        scroll.setContent(form);
        addAndMakeVisible(scroll);
    }

    void paint(Widgets::Graphics& g) override
    {
        g.fillAll(getHostTheme().get(Widgets::ThemeKeys::background));
    }

    void resized() override { scroll.setBounds(getLocalBounds()); }

    const SettingsPanelOptions& options() const { return form.options; }

    Snapshot takeSnapshot() const
    {
        auto result = Snapshot {};
        result.devices = devices.getDevices();

        if (options().showMidiInputs)
            result.midiInputs = midi.getInputPorts();

        if (options().showMidiOutput)
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

    // The form's height follows a list that grew or shrank.
    void relayOut()
    {
        form.updateHeight();
        form.resized();
        scroll.resized();
    }

    void rebuild()
    {
        auto out = currentDevice(config.output);
        auto in = currentDevice(config.input);
        auto outId = config.output ? config.output->device.id : noneId;
        auto inId = config.input ? config.input->device.id : noneId;

        form.output.fill(UI::makeOutputDeviceDropdown(snapshot.devices, outId));

        auto listsChanged = form.outputChannels.fill(
            config.output ? channelChoices(UI::makeOutputChannelDropdown(
                                               out,
                                               config.output->firstChannel,
                                               config.output->nChannels),
                                           out.outputChannels,
                                           options())
                          : UI::DropdownInfo {});

        if (options().showInput)
        {
            listsChanged |= form.setInputChosen(config.input.has_value());

            form.input.fill(
                withNone(UI::makeInputDeviceDropdown(snapshot.devices, inId)));

            listsChanged |= form.inputChannels.fill(
                config.input ? channelChoices(UI::makeInputChannelDropdown(
                                                  in,
                                                  config.input->firstChannel,
                                                  config.input->nChannels),
                                              in.inputChannels,
                                              options())
                             : UI::DropdownInfo {});
        }

        const auto& clocked = config.output ? out : in;

        form.rate.fill(UI::makeSampleRateDropdown(clocked, config.sampleRate));
        form.block.fill(blockSizeChoices(getSupportedBlockSizes(clocked),
                                         config.maxBlockSize,
                                         config.sampleRate,
                                         options()));

        if (options().showMidiInputs)
            listsChanged |= rebuildMidiInputs();

        if (options().showMidiOutput)
            rebuildMidiOutput();

        if (listsChanged)
            relayOut();
    }

    // Recreated only when the ports changed, since a checkbox may be the one
    // whose onChange is running. True when they did.
    bool rebuildMidiInputs()
    {
        if (form.midiInputsInList())
            return form.midiInputList.fill(midiInputItems());

        const auto& ports = snapshot.midiInputs;
        auto changed = !std::ranges::equal(ports, shownMidiInputs);

        if (changed)
        {
            form.midiInputs.clear();
            shownMidiInputs = ports;

            for (const auto& port: ports)
            {
                auto& box = *form.midiInputs.add(
                    EA::makeOwned<Widgets::Checkbox>(port.name));
                auto id = port.id;

                box.onChange = [this, id](bool on) { toggleMidiInput(id, on); };

                form.addAndMakeVisible(box);
            }

            form.noMidiInputs.setVisible(ports.empty());
        }

        showOpenMidiInputs();
        return changed;
    }

    Vector<CheckList::Item> midiInputItems() const
    {
        auto items = Vector<CheckList::Item> {};

        for (const auto& port: snapshot.midiInputs)
            items.create(port.id, port.name, midi.isInputOpen(port.id));

        return items;
    }

    void toggleMidiInput(int id, bool on)
    {
        if (owner.onMidiInputToggled)
            owner.onMidiInputToggled(id, on);

        showOpenMidiInputs();
    }

    void addAction(std::string label, std::function<void()> onClick)
    {
        auto& button =
            *form.actions.add(EA::makeOwned<Widgets::Button>(std::move(label)));
        button.onClick = std::move(onClick);

        form.addAndMakeVisible(button);
        relayOut();
    }

    void showOpenMidiInputs()
    {
        if (form.midiInputsInList())
        {
            form.midiInputList.fill(midiInputItems());
            return;
        }

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

    void pickChannels(std::optional<StreamParameters>& side, int id)
    {
        if (!side)
            return;

        auto selection = UI::decodeChannelSelection(id);
        side->firstChannel = selection.firstChannel;
        side->nChannels = selection.count;
        changed();
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

        auto channels =
            [this](ChannelPicker& picker, std::optional<StreamParameters>& side)
        {
            picker.choice.box.onChange = [this, &picker, &side](int index)
            {
                if (auto id = picker.choice.idAt(index))
                    pickChannels(side, *id);
            };

            // The ticked channel is the selection: unticking it puts the tick
            // back, ticking another moves the selection there.
            picker.list.onToggle = [this, &picker, &side](int id, bool checked)
            {
                if (checked)
                    pickChannels(side, id);
                else
                    picker.list.setChecked(id, true);
            };
        };

        channels(form.outputChannels, config.output);
        channels(form.inputChannels, config.input);

        form.midiInputList.onToggle = [this](int id, bool on)
        { toggleMidiInput(id, on); };

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

        form.testButton.onClick = [this]
        {
            if (owner.onTestOutput)
                owner.onTestOutput();
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
    : content(EA::makeOwned<Content>(
          *this, devicesToUse, midiToUse, std::move(optionsToUse)))
{
    // The backdrop is the theme's background already, and follows the theme an
    // app sets later.
    setFontPointSize(content->form.metrics.fontSize);
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

    if (content->options().showMidiOutput)
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

void SettingsPanel::addAction(std::string label, std::function<void()> onClick)
{
    content->addAction(std::move(label), std::move(onClick));
}

int SettingsPanel::preferredWidth() const
{
    return static_cast<int>(content->form.metrics.width);
}

int SettingsPanel::preferredHeight() const
{
    return static_cast<int>(content->form.naturalHeight());
}

const SettingsPanelOptions& SettingsPanel::getOptions() const
{
    return content->options();
}

const SettingsPanelMetrics& SettingsPanel::getMetrics() const
{
    return content->form.metrics;
}

} // namespace MakeASound::Standalone
