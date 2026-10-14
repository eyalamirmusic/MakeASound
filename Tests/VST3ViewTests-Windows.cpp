// The Windows half of the VST3 view's scaling: a ViewRect is in physical pixels
// there, and a host that never calls setContentScaleFactor leaves the window's
// DPI as the only scale to go by.

#include "VST3TestHost.h"

#include <MakeASound/Plugin/UI/GenericEditor.h>
#include <MakeASound/Plugin/VST3/PlugView.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Utils/WinInclude.h>

#include <cmath>

using namespace nano;
using namespace TestPlugins;
using namespace VST3Host;

namespace
{
Steinberg::ViewRect sizeOf(Steinberg::IPlugView& view)
{
    auto rect = Steinberg::ViewRect {};
    check(view.getSize(&rect) == Steinberg::kResultOk);
    return rect;
}

int scaled(int points, float factor)
{
    return static_cast<int>(std::lround(static_cast<float>(points) * factor));
}

auto tUnscaled = test("VST3/anUnscaledWindowsViewFollowsTheDpi") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto view = Steinberg::owned(host.adapter->createView(Vst::ViewType::kEditor));
    check(view != nullptr);

    auto points = MakeASound::GenericEditor(host.adapter->plugin()).initialSize();
    auto dpi = static_cast<float>(GetDpiForSystem()) / 96.f;

    // Not yet parented: the system's DPI is the guess, as eacp's own.
    auto unscaled = sizeOf(*view);
    check(unscaled.getWidth() == scaled(points.width, dpi));
    check(unscaled.getHeight() == scaled(points.height, dpi));

    // Once the host names a scale, it wins over the DPI.
    auto scaling =
        Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>(view.get());
    check(scaling->setContentScaleFactor(2.f) == Steinberg::kResultOk);

    auto hostScaled = sizeOf(*view);
    check(hostScaled.getWidth() == points.width * 2);
    check(hostScaled.getHeight() == points.height * 2);
};
} // namespace
