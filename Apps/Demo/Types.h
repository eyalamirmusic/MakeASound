#pragma once

#include <MakeASound/MakeASound.h>
#include <Miro/Miro.h>
#include <eacp/Core/Core.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <string>
#include <utility>

struct AudioControls
{
    MIRO_REFLECT(playing, gain)

    bool playing {};
    double gain {};
};

struct MeterState
{
    MIRO_REFLECT(inputLevel)

    double inputLevel {};
};

struct UIState
{
    MIRO_REFLECT(blockSize, devices, inputDevices, sampleRates, midiPorts)

    int blockSize {};
    MakeASound::UI::DropdownInfo devices;
    MakeASound::UI::DropdownInfo inputDevices;
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
                   &T::setInputDevice,
                   &T::midiPortToggle>();

        r.events<&T::ui, &T::audio, &T::meter, &T::midi>();
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
        if (applyOutputDevice(id))
            ui.publish(makeUi());
    }

    void setInputDevice(const int& id)
    {
        if (applyInputDevice(id))
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

    void pollMeter() { meter.publish(makeMeter()); }

    Miro::Event<UIState> ui;
    Miro::Event<AudioControls> audio;
    Miro::Event<MeterState> meter;
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
        // Metering only — the input is never written to the output.
        auto peak = 0.0f;

        for (auto channel: info.getInput().channels())
            for (auto sample: channel)
                peak = std::max(peak, std::abs(sample));

        inputLevelValue.store(peak, std::memory_order_relaxed);

        auto on = playing.load(std::memory_order_relaxed);
        auto g = gainValue.load(std::memory_order_relaxed);

        for (auto channel: info.getOutput().channels())
        {
            if (!on)
            {
                std::ranges::fill(channel, 0.0f);
                continue;
            }

            for (auto& sample: channel)
                sample = nextNoiseSample() * g;
        }
    }

    bool applyOutputDevice(int deviceId)
    {
        for (auto& device: manager.getDevices())
        {
            if (device.id != deviceId || device.outputChannels == 0)
                continue;

            config.output = MS::StreamParameters {device, false};
            reconcileSampleRate();
            manager.setConfig(config);
            return true;
        }

        return false;
    }

    bool applyInputDevice(int deviceId)
    {
        for (auto& device: manager.getDevices())
        {
            if (device.id != deviceId || device.inputChannels == 0)
                continue;

            config.input = MS::StreamParameters {device, true};
            reconcileSampleRate();
            manager.setConfig(config);
            return true;
        }

        return false;
    }

    // A duplex stream needs a rate both devices can drive; fall back to the
    // configured device's own list when only one side is set.
    void reconcileSampleRate()
    {
        if (config.output && config.input)
        {
            config.sampleRate = MS::pickCompatibleSampleRate(config.output->device,
                                                             config.input->device);
            return;
        }

        auto& device = config.output ? config.output->device : config.input->device;

        if (!device.sampleRates.empty()
            && std::ranges::find(device.sampleRates, config.sampleRate)
                   == device.sampleRates.end())
            config.sampleRate = device.sampleRates.front();
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

    MeterState makeMeter() const
    { return {.inputLevel = static_cast<double>(inputLevelValue.load())}; }

    UIState makeUi()
    {
        auto state = UIState {};
        state.blockSize = config.maxBlockSize;

        auto currentDeviceId = config.output ? config.output->device.id : 0;
        state.devices = uiDevices.makeOutputDeviceDropdown(currentDeviceId);

        auto currentInputId = config.input ? config.input->device.id : 0;
        state.inputDevices = uiDevices.makeInputDeviceDropdown(currentInputId);

        if (config.output)
            state.sampleRates =
                uiDevices.makeSampleRateDropdown(currentDeviceId, config.sampleRate);

        lastInputPorts = midiManager.getInputPorts();
        state.midiPorts = uiMidi.makeInputPortToggleList();
        return state;
    }

    std::atomic<bool> playing {false};
    std::atomic<float> gainValue {0.1f};
    std::atomic<float> inputLevelValue {0.0f};
    MS::DeviceManager manager;
    MS::MidiManager midiManager;
    MS::UIDeviceManager uiDevices {manager};
    MS::UIMidiManager uiMidi {midiManager};
    MS::StreamConfig config;
    MS::Vector<MS::MidiPortInfo> lastInputPorts;
};

} // namespace Api
