#pragma once

#include "../Common/Common.h"
#include <Miro/Miro.h>

#include <string>

namespace MakeASound
{

struct Bus
{
    bool operator==(const Bus&) const = default;

    std::string name = "Audio";
    int numChannels = 2;
    bool isMain = true;

    MIRO_REFLECT(name, numChannels, isMain)
};

struct MidiBus
{
    bool operator==(const MidiBus&) const = default;

    std::string name = "MIDI";

    MIRO_REFLECT(name)
};

// What a Processor reads and writes: bus 0 on each side is the main one by
// convention, and what nearly every process() is written against.
struct BusLayout
{
    bool operator==(const BusLayout&) const = default;

    static BusLayout stereoInOut(int numChannels = 2)
    {
        auto layout = BusLayout {};
        layout.inputs.add({"Input", numChannels});
        layout.outputs.add({"Output", numChannels});
        return layout;
    }

    static BusLayout stereoOut(int numChannels = 2)
    {
        auto layout = BusLayout {};
        layout.outputs.add({"Output", numChannels});
        return layout;
    }

    // An output bus fed by a MIDI input bus.
    static BusLayout instrument(int numChannels = 2)
    {
        auto layout = stereoOut(numChannels);
        layout.midiInputs.add({"MIDI"});
        return layout;
    }

    // Same bus count on every side and the same channel count on every audio
    // bus; names are not compared.
    bool acceptsExactly(const BusLayout& proposed) const noexcept
    {
        auto sameChannels = [](const Vector<Bus>& a, const Vector<Bus>& b)
        {
            if (a.size() != b.size())
                return false;

            for (auto i = 0; i < a.size(); ++i)
                if (a[i].numChannels != b[i].numChannels)
                    return false;

            return true;
        };

        return sameChannels(inputs, proposed.inputs)
               && sameChannels(outputs, proposed.outputs)
               && midiInputs.size() == proposed.midiInputs.size()
               && midiOutputs.size() == proposed.midiOutputs.size();
    }

    int getMainInputChannels() const noexcept
    {
        return inputs.empty() ? 0 : inputs[0].numChannels;
    }

    int getMainOutputChannels() const noexcept
    {
        return outputs.empty() ? 0 : outputs[0].numChannels;
    }

    Vector<Bus> inputs;
    Vector<Bus> outputs;
    Vector<MidiBus> midiInputs;
    Vector<MidiBus> midiOutputs;

    MIRO_REFLECT(inputs, outputs, midiInputs, midiOutputs)
};

} // namespace MakeASound
