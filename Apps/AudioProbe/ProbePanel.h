#pragma once

#include "AudioEngine.h"
#include "Probes.h"

#include <eacp/UI/UI.h>

namespace AudioProbe
{
namespace UI = eacp::UI;

// The controls, and the probe findings as a list: what the app is for, in the
// component tier, so the same tree draws on a desktop window and on a phone.
class ProbeRoot final
    : public UI::Component
    , public UI::ListBoxModel
{
public:
    ProbeRoot(AudioEngine& engineToUse, ProbeSet& probesToUse);

    // `rebuildDevices` re-enumerates, which is why it is not done every tick.
    void refresh(bool rebuildDevices);

    void paint(UI::Graphics& g) override;
    void resized() override;

    int getNumRows() override;
    void paintRow(UI::Graphics& g,
                  int row,
                  const UI::Rect& bounds,
                  bool selected) override;
    void selectedRowChanged(int row) override;

private:
    void wireControls();
    void rebuildDeviceBoxes();
    void showDetail(int row);

    // A phone is not wide enough for a probe's sentence, so what does not fit
    // is cut rather than drawn under the status column.
    float getHostFontSize() const;
    std::string elide(const std::string& text, float width, float pointSize) const;

    AudioEngine& engine;
    ProbeSet& probes;

    UI::Label title {"MakeASound probe"};
    UI::Label summary;
    UI::Label detail;
    UI::Label footer;

    UI::Knob tone;
    UI::Knob level;
    UI::Label toneCaption;
    UI::Label levelCaption;

    UI::Checkbox monitor {"Capture side"};
    UI::Checkbox recover {"Auto-recover"};

    UI::Label deviceCaption {"output"};
    UI::Label rateCaption {"rate"};
    UI::Label blockCaption {"block"};

    UI::ComboBox device {"device"};
    UI::ComboBox rate {"rate"};
    UI::ComboBox block {"size"};

    UI::ListBox list;

    MS::Vector<int> deviceIds;
    MS::Vector<int> rateValues;
    MS::Vector<int> blockValues;
};

class ProbePanel final : public UI::ComponentHost
{
public:
    ProbePanel(AudioEngine& engine, ProbeSet& probes);

    void refresh(bool rebuildDevices) { root.refresh(rebuildDevices); }

private:
    ProbeRoot root;
};

} // namespace AudioProbe
