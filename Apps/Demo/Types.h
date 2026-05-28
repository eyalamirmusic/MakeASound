#pragma once

#include <MakeASound/MakeASound.h>
#include <Miro/Miro.h>
#include <eacp/Core/Core.h>

#include <algorithm>
#include <atomic>
#include <random>
#include <string>
#include <utility>

struct AudioControls
{
    MIRO_REFLECT(playing, gain)

    bool playing {};
    double gain {};
};

struct UIState
{
    MIRO_REFLECT(blockSize, devices, sampleRates, midiPorts)

    int blockSize {};
    MakeASound::UI::DropdownInfo devices;
    MakeASound::UI::DropdownInfo sampleRates;
    MakeASound::UI::ToggleListInfo midiPorts;
};

struct MidiPortToggleRequest
{
    MIRO_REFLECT(id, on)

    int id {};
    bool on {};
};

struct MidiLogEntry
{
    MIRO_REFLECT(text)

    std::string text;
};

namespace Api
{
namespace MS = MakeASound;

class DemoApi
{
public:
    DemoApi()
    {
        config = manager.getDefaultConfig();
        manager.start(config, [this](auto& info) { renderWhiteNoise(info); });
    }

    void reflect(Miro::ApiReflector& r)
    {
        using T = DemoApi;

        r.commands<&T::getUi,
                   &T::getAudio,
                   &T::setPlaying,
                   &T::setGain,
                   &T::setSampleRate,
                   &T::setBlockSize,
                   &T::setDevice,
                   &T::midiPortToggle>();

        r.events<&T::ui, &T::audio, &T::midi>();
    }

    UIState getUi() { return makeUi(); }
    AudioControls getAudio() const { return makeControls(); }

    void setPlaying(const bool& value)
    {
        playing.store(value);
        audio.publish(makeControls());
    }

    void setGain(const double& value)
    {
        gainValue.store(static_cast<float>(value));
        audio.publish(makeControls());
    }

    void setSampleRate(const int& value)
    {
        config.sampleRate = value;
        manager.setConfig(config);
        ui.publish(makeUi());
    }

    void setBlockSize(const int& value)
    {
        config.maxBlockSize = value;
        manager.setConfig(config);
        ui.publish(makeUi());
    }

    void setDevice(const int& id)
    {
        if (applyDevice(id))
            ui.publish(makeUi());
    }

    void midiPortToggle(const MidiPortToggleRequest& req)
    {
        if (req.on)
            midiManager.openInput(req.id,
                                  [this](const MS::MidiMessage& msg)
                                  { handleIncomingMidi(msg); });
        else
            midiManager.closeInput(req.id);

        ui.publish(makeUi());
    }

    void pollMidiPorts()
    {
        auto current = midiManager.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = std::move(current);
        ui.publish(makeUi());
    }

    Miro::Event<UIState> ui;
    Miro::Event<AudioControls> audio;
    Miro::Event<MidiLogEntry> midi;

private:
    static float nextNoiseSample()
    {
        static auto engine = std::default_random_engine {std::random_device {}()};
        static auto dist = std::uniform_real_distribution<float> {-1.0f, 1.0f};
        return dist(engine);
    }

    void renderWhiteNoise(MS::AudioCallbackInfo& info)
    {
        auto on = playing.load(std::memory_order_relaxed);
        auto g = gainValue.load(std::memory_order_relaxed);

        for (auto channel = 0; channel < info.numOutputs; ++channel)
        {
            auto data = info.getOutput(channel);

            if (!on)
            {
                std::ranges::fill(data, 0.0f);
                continue;
            }

            for (auto& sample: data)
                sample = nextNoiseSample() * g;
        }
    }

    bool applyDevice(int deviceId)
    {
        for (auto& device: manager.getDevices())
        {
            if (device.id != deviceId || device.outputChannels == 0)
                continue;

            config.output = MS::StreamParameters {device, false};

            if (!device.sampleRates.empty()
                && std::ranges::find(device.sampleRates, config.sampleRate)
                       == device.sampleRates.end())
                config.sampleRate = device.sampleRates.front();

            manager.setConfig(config);
            return true;
        }

        return false;
    }

    void handleIncomingMidi(const MS::MidiMessage& msg)
    {
        if (msg.bytes.size() >= 3)
        {
            auto status = msg.bytes[0] & 0xF0;
            auto data1 = msg.bytes[1];
            auto data2 = msg.bytes[2];

            if (status == 0x90 && data2 > 0)
            {
                playing.store(true);
                gainValue.store(static_cast<float>(data2) / 127.0f);
            }
            else if (status == 0x80 || (status == 0x90 && data2 == 0))
            {
                playing.store(false);
            }
            else if (status == 0xB0 && data1 == 7)
            {
                gainValue.store(static_cast<float>(data2) / 127.0f);
            }
        }

        eacp::Threads::callAsync(
            [this, msg]
            {
                midi.publish({MS::formatMessage(msg)});
                audio.publish(makeControls());
            });
    }

    AudioControls makeControls() const
    {
        return {.playing = playing.load(),
                .gain = static_cast<double>(gainValue.load())};
    }

    UIState makeUi()
    {
        auto state = UIState {};
        state.blockSize = config.maxBlockSize;

        auto currentDeviceId = config.output ? config.output->device.id : 0;
        state.devices = uiDevices.makeOutputDeviceDropdown(currentDeviceId);

        if (config.output)
            state.sampleRates =
                uiDevices.makeSampleRateDropdown(currentDeviceId, config.sampleRate);

        lastInputPorts = midiManager.getInputPorts();
        state.midiPorts = uiMidi.makeInputPortToggleList();
        return state;
    }

    std::atomic<bool> playing {false};
    std::atomic<float> gainValue {0.1f};
    MS::DeviceManager manager;
    MS::MidiManager midiManager;
    MS::UIDeviceManager uiDevices {manager};
    MS::UIMidiManager uiMidi {midiManager};
    MS::StreamConfig config;
    MS::Vector<MS::MidiPortInfo> lastInputPorts;
};

} // namespace Api
