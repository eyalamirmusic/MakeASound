#include "ProbePanel.h"

#include <algorithm>
#include <cmath>

namespace AudioProbe
{
namespace
{
constexpr auto padding = 12.f;

bool onPhone()
{ return eacp::Platform::isIOS(); }

float rowHeight()
{ return onPhone() ? 58.f : 46.f; }

float smallText()
{ return onPhone() ? 12.f : 11.f; }

UI::Color statusColour(ProbeStatus status)
{
    switch (status)
    {
        case ProbeStatus::Gap:
            return {0.98f, 0.42f, 0.38f, 1.f};
        case ProbeStatus::Pass:
            return {0.38f, 0.85f, 0.55f, 1.f};
        case ProbeStatus::NotApplicable:
        case ProbeStatus::Pending:
        default:
            return UI::defaultTheme().dimText;
    }
}

float toneToHertz(float knob)
{ return 40.f * std::pow(2.f, knob * 7.f); }

float hertzToTone(float hertz)
{ return std::log2(std::max(hertz, 40.f) / 40.f) / 7.f; }

std::string wholeNumber(float value)
{ return std::to_string(static_cast<int>(std::lround(value))); }
} // namespace

ProbeRoot::ProbeRoot(AudioEngine& engineToUse, ProbeSet& probesToUse)
    : engine(engineToUse)
    , probes(probesToUse)
{
    title.setFontSize(onPhone() ? 17.f : 15.f);
    summary.setJustification(UI::Justification::Right);

    for (auto* label: {&detail,
                       &footer,
                       &toneCaption,
                       &levelCaption,
                       &deviceCaption,
                       &channelCaption,
                       &rateCaption,
                       &blockCaption})
    {
        label->setColour(UI::defaultTheme().dimText);
        label->setFontSize(smallText());
    }

    list.setModel(this);
    list.setRowHeight(rowHeight());

    wireControls();

    addChildren({title, summary, tone, toneCaption, level, levelCaption});
    addChildren({monitor, recover, deviceCaption, device});
    addChildren({channelCaption, channels, rateCaption, rate});
    addChildren({blockCaption, block, list, detail, footer});
}

void ProbeRoot::wireControls()
{
    tone.setValue(hertzToTone(engine.toneHz.load()));
    level.setValue(engine.levelGain.load() * 2.f);
    recover.setChecked(engine.isAutoRecovering());

    tone.onValueChange = [this](float value)
    {
        auto hertz = toneToHertz(value);
        engine.toneHz.store(hertz);
        toneCaption.setText("tone " + wholeNumber(hertz) + " Hz");
    };

    level.onValueChange = [this](float value)
    {
        engine.levelGain.store(value * 0.5f);
        levelCaption.setText("level " + wholeNumber(value * 100.f) + "%");
    };

    // Opening the capture side is the whole iOS story in one checkbox: it is
    // what turns the session into PlayAndRecord and asks for the microphone.
    monitor.onChange = [this](bool on)
    {
        engine.monitorInput.store(on);
        engine.setInputEnabled(on);
        refresh(true);
    };

    recover.onChange = [this](bool on)
    {
        engine.setAutoRecover(on);
        refresh(false);
    };

    device.onChange = [this](int index)
    {
        if (index >= 0 && index < deviceIds.size())
            engine.setOutputDevice(deviceIds[index]);

        refresh(true);
    };

    channels.onChange = [this](int index)
    {
        if (index >= 0 && index < channelValues.size())
        {
            auto selection = MS::UI::decodeChannelSelection(channelValues[index]);
            engine.setOutputChannels(selection.firstChannel, selection.count);
        }

        refresh(true);
    };

    rate.onChange = [this](int index)
    {
        if (index >= 0 && index < rateValues.size())
            engine.setSampleRate(rateValues[index]);

        refresh(true);
    };

    block.onChange = [this](int index)
    {
        if (index >= 0 && index < blockValues.size())
            engine.setBlockSize(blockValues[index]);

        refresh(true);
    };

    toneCaption.setText("tone " + wholeNumber(engine.toneHz.load()) + " Hz");
    levelCaption.setText("level " + wholeNumber(engine.levelGain.load() * 200.f)
                         + "%");
}

void ProbeRoot::rebuildDeviceBoxes()
{
    auto helper = MS::UIDeviceManager {engine.getManager()};
    const auto& config = engine.getConfig();
    auto currentId = config.output.has_value() ? config.output->device.id : -1;

    auto fill =
        [](UI::ComboBox& box, MS::Vector<int>& ids, const MS::UI::DropdownInfo& info)
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
    };

    auto firstChannel = config.output.has_value() ? config.output->firstChannel : 0;
    auto channelCount = config.output.has_value() ? config.output->nChannels : 2;

    fill(device, deviceIds, helper.makeOutputDeviceDropdown(currentId));
    fill(channels,
         channelValues,
         helper.makeOutputChannelDropdown(currentId, firstChannel, channelCount));
    fill(rate,
         rateValues,
         helper.makeSampleRateDropdown(currentId, config.sampleRate));
    fill(block,
         blockValues,
         helper.makeBlockSizeDropdown(currentId, config.maxBlockSize));
}

void ProbeRoot::refresh(bool rebuildDevices)
{
    if (rebuildDevices)
        rebuildDeviceBoxes();

    monitor.setChecked(engine.isInputEnabled());

    summary.setText(std::to_string(probes.count(ProbeStatus::Gap)) + " gaps   "
                    + std::to_string(probes.count(ProbeStatus::Pass)) + " pass   "
                    + std::to_string(probes.count(ProbeStatus::Pending))
                    + " waiting");

    auto stats = engine.getStats();
    auto& manager = engine.getManager();

    auto slice = std::string {};

    if (const auto& out = engine.getConfig().output; out.has_value())
        slice = "   out ch " + std::to_string(out->firstChannel + 1) + "-"
                + std::to_string(out->firstChannel + out->nChannels) + " of "
                + std::to_string(out->device.outputChannels);

    auto line = MS::getBackendName(manager.getBackend()) + slice + "   "
                + std::to_string(stats.sampleRate) + " Hz   blocks of "
                + std::to_string(stats.lastNumSamples) + "   "
                + std::to_string(stats.outputs) + " out / "
                + std::to_string(stats.inputs) + " in   latency "
                + std::to_string(stats.latency) + "   "
                + std::to_string(stats.callbacks)
                + " callbacks   last event: " + probes.getLastNotification();

    if (manager.getLastError() != MS::Error::NoError)
        line += "   error: " + MS::getErrorMessage(manager.getLastError());

    if (probes.getSession().available)
        line += "   session: " + describe(probes.getSession());

    footer.setText(line);

    list.updateContent();
}

int ProbeRoot::getNumRows()
{ return probes.all().size(); }

void ProbeRoot::paintRow(UI::Graphics& g,
                         int row,
                         const UI::Rect& bounds,
                         bool selected)
{
    const auto& theme = UI::defaultTheme();
    const auto& probe = probes.all()[row];

    if (selected)
    {
        g.setColour(theme.accentDim);
        g.fillRect(bounds);
    }
    else if (row % 2 == 1)
    {
        g.setColour(theme.panel);
        g.fillRect(bounds);
    }

    auto area = bounds.inset(10.f, 6.f);
    auto top = area.removeFromTop(area.h * 0.5f);
    auto statusArea = top.removeFromRight(70.f);

    g.setColour(statusColour(probe.status));
    g.drawText(toString(probe.status), statusArea, UI::Justification::Right);

    g.setColour(theme.text);
    g.drawText(elide(probe.id, top.w, getHostFontSize()), top);

    auto detailText = probe.actual.empty() ? probe.expected : probe.actual;

    g.setColour(theme.dimText);
    g.setFontSize(smallText());
    g.drawText(elide(detailText, area.w, smallText()), area);
}

float ProbeRoot::getHostFontSize() const
{
    auto* host = getHost();
    return host != nullptr ? host->getFont().pointSize : 13.f;
}

std::string
    ProbeRoot::elide(const std::string& text, float width, float pointSize) const
{
    auto* host = getHost();

    if (host == nullptr || width <= 0.f)
        return text;

    auto font = host->getFont();
    font.pointSize = pointSize;

    auto full = measureText(text, font);

    if (full <= width)
        return text;

    // Proportional first guess, then a few characters either way: measuring one
    // character at a time would be a hundred calls a row.
    auto length = std::clamp(static_cast<int>(text.size() * (width / full)) - 3,
                             0,
                             static_cast<int>(text.size()));

    while (length > 0 && measureText(text.substr(0, length) + "...", font) > width)
        --length;

    return text.substr(0, length) + "...";
}

void ProbeRoot::selectedRowChanged(int row)
{ showDetail(row); }

void ProbeRoot::showDetail(int row)
{
    if (row < 0 || row >= probes.all().size())
    {
        detail.setText({});
        return;
    }

    const auto& probe = probes.all()[row];
    detail.setText("expected: " + probe.expected
                   + "    |    actual: " + probe.actual);
}

void ProbeRoot::paint(UI::Graphics& g)
{ g.fillAll(UI::defaultTheme().background); }

void ProbeRoot::resized()
{
    auto area = getLocalBounds().inset(padding);

    auto header = area.removeFromTop(22.f);
    title.setBounds(header.removeFromLeft(header.w * 0.5f));
    summary.setBounds(header);

    area.removeFromTop(8.f);

    auto wide = area.w >= 620.f;

    auto knobs = area.removeFromTop(56.f);
    tone.setBounds(knobs.removeFromLeft(52.f).inset(2.f));
    toneCaption.setBounds(knobs.removeFromLeft(86.f));
    level.setBounds(knobs.removeFromLeft(52.f).inset(2.f));
    levelCaption.setBounds(knobs.removeFromLeft(86.f));

    auto toggles = wide ? knobs.removeFromRight(std::min(knobs.w, 280.f))
                        : area.removeFromTop(26.f);

    monitor.setBounds(toggles.removeFromLeft(toggles.w * 0.5f));
    recover.setBounds(toggles);

    area.removeFromTop(6.f);

    auto outputRow = area.removeFromTop(26.f);
    deviceCaption.setBounds(outputRow.removeFromLeft(52.f));
    device.setBounds(outputRow);

    area.removeFromTop(6.f);

    auto channelRow = area.removeFromTop(26.f);
    channelCaption.setBounds(channelRow.removeFromLeft(52.f));
    channels.setBounds(channelRow);

    area.removeFromTop(6.f);

    auto formatRow = area.removeFromTop(26.f);
    rateCaption.setBounds(formatRow.removeFromLeft(52.f));
    rate.setBounds(formatRow.removeFromLeft(std::min(formatRow.w * 0.45f, 130.f)));
    formatRow.removeFromLeft(10.f);
    blockCaption.setBounds(formatRow.removeFromLeft(52.f));
    block.setBounds(formatRow.removeFromLeft(std::min(formatRow.w, 130.f)));

    area.removeFromTop(10.f);

    footer.setBounds(area.removeFromBottom(18.f));
    detail.setBounds(area.removeFromBottom(18.f));
    area.removeFromBottom(6.f);

    list.setBounds(area);
}

ProbePanel::ProbePanel(AudioEngine& engine, ProbeSet& probes)
    : root(engine, probes)
{
    setFontPointSize(onPhone() ? 14.f : 13.f);
    setBackgroundColour(UI::defaultTheme().background);
    setRootComponent(root);

    // On iOS a component tier's on-demand repaint does not reach the screen, so
    // the panel would sit on the frame it was built with while the readings move
    // underneath it. Rendering every refresh is the way to see them there.
    if (onPhone())
        setContinuous(true);
}

} // namespace AudioProbe
