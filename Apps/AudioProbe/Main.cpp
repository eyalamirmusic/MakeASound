#include "ProbePanel.h"
#include "SpectrumView.h"

#include <eacp/Core/Core.h>
#include <eacp/Graphics/Graphics.h>

#include <algorithm>
#include <cassert>
#include <string_view>

namespace AudioProbe
{
namespace
{
// --strict runs the probes for a couple of seconds, logs every gap and exits
// with the number of them, which is what makes this usable from CI. --assert
// trips an assertion on the first gap instead, for a debugger.
bool strictMode = false;
bool assertMode = false;

eacp::Graphics::WindowOptions makeOptions()
{
    auto options = eacp::Graphics::WindowOptions {};
    options.width = 1040;
    options.height = 760;
    options.minWidth = 520;
    options.minHeight = 460;
    options.title = "MakeASound probe";

    return options;
}
} // namespace

// The visualizer and the panel are two native views side by side rather than
// one: a ComponentHost is a GPUView drawing the widget tier, and the spectrum
// is a GPUView drawing a shader of its own.
struct Root final : eacp::Graphics::View
{
    Root(SpectrumView& spectrumToUse, ProbePanel& panelToUse)
        : spectrum(spectrumToUse)
        , panel(panelToUse)
    { addChildren({spectrum, panel}); }

    void resized() override
    {
        auto area = getLocalBounds();

        // The status bar is hidden on iOS; the notch is still there.
        if (eacp::Platform::isIOS())
            area.removeFromTop(48.f);

        spectrum.setBounds(area.removeFromTop(std::max(area.h * 0.32f, 120.f)));
        panel.setBounds(area);
    }

    SpectrumView& spectrum;
    ProbePanel& panel;
};

struct App
{
    App()
    {
        spectrum.sampleRate = [this]
        {
            auto running = engine.getStats().sampleRate;
            return running > 0 ? running : engine.getRequestedSampleRate();
        };

        engine.start();

        probes.refresh();
        panel.refresh(true);

        window.setContentView(root);
    }

    void tick()
    {
        for (const auto& event: engine.drainEvents())
            probes.observe(event);

        ++ticks;

        if (ticks % 8 == 0)
            probes.refresh();

        panel.refresh(ticks % 32 == 0);

        auto gaps = probes.count(ProbeStatus::Gap);

        if (assertMode && gaps > 0)
        {
            probes.logGaps();
            assert(gaps == 0 && "MakeASound probes found gaps - see the log");
        }

        if (strictMode && ticks == 24)
        {
            probes.logGaps();
            eacp::Apps::quit(gaps);
        }
    }

    AudioEngine engine;
    ProbeSet probes {engine};

    SpectrumView spectrum {engine.getAnalyser()};
    ProbePanel panel {engine, probes};
    Root root {spectrum, panel};

    eacp::Graphics::Window window {makeOptions()};
    eacp::Threads::Timer timer {[this] { tick(); }, 8};

    int ticks = 0;
};

} // namespace AudioProbe

int main(int argc, char** argv)
{
    for (auto i = 1; i < argc; ++i)
    {
        auto argument = std::string_view {argv[i]};

        AudioProbe::strictMode |= argument == "--strict";
        AudioProbe::assertMode |= argument == "--assert";
    }

    return eacp::Apps::run<AudioProbe::App>();
}
