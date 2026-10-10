#pragma once

#include "SynthHost.h"

#include <MakeASound/MakeASound.h>
#include <Miro/Miro.h>
#include <eacp/Core/Core.h>

#include <utility>

struct UIState
{
    MakeASound::UI::DropdownInfo devices;
    MakeASound::UI::DropdownInfo sampleRates;
    MakeASound::UI::DropdownInfo blockSizes;
    MakeASound::UI::ToggleListInfo midiPorts;

    MIRO_REFLECT(devices, sampleRates, blockSizes, midiPorts)
};

struct MidiPortToggleRequest
{
    int id {};
    bool on {};

    MIRO_REFLECT(id, on)
};

struct MidiLogEntry
{
    std::string text;

    MIRO_REFLECT(text)
};

namespace Api
{

class SynthApi
{
public:
    void reflect(Miro::ApiReflector& r)
    {
        using T = SynthApi;

        r.commands<&T::getUi,
                   &T::getAudio,
                   &T::setGain,
                   &T::setSampleRate,
                   &T::setBlockSize,
                   &T::setDevice,
                   &T::midiPortToggle,
                   &T::allNotesOff>();

        r.events<&T::ui, &T::audio, &T::midi>();
    }

    UIState getUi() { return makeUi(); }
    AudioControls getAudio() const { return host.synth.makeControls(); }

    void setGain(const double& value)
    {
        host.synth.setGain(static_cast<float>(value));
        audio.publish(host.synth.makeControls());
    }

    void setSampleRate(const int& value)
    {
        host.applySampleRate(value);
        ui.publish(makeUi());
    }

    void setBlockSize(const int& value)
    {
        host.applyBlockSize(value);
        ui.publish(makeUi());
    }

    void setDevice(const int& id)
    {
        if (host.applyDevice(id))
            ui.publish(makeUi());
    }

    void midiPortToggle(const MidiPortToggleRequest& req)
    {
        host.applyMidiPortToggle(req.id, req.on);
        ui.publish(makeUi());
    }

    void allNotesOff() { host.synth.releaseAllNotes(); }

    void pollMidiPorts()
    {
        auto current = host.midi.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = std::move(current);
        ui.publish(makeUi());
    }

    Miro::Event<UIState> ui;
    Miro::Event<AudioControls> audio;
    Miro::Event<MidiLogEntry> midi;

private:
    void onMidiApplied(const MIDI::Event& event)
    {
        midi.publish({MIDI::toString(event)});
        audio.publish(host.synth.makeControls());
    }

    UIState makeUi()
    {
        auto& config = host.config;
        auto state = UIState {};

        auto currentDeviceId = config.output ? config.output->device.id : 0;
        state.devices = uiDevices.makeOutputDeviceDropdown(currentDeviceId);

        if (config.output)
        {
            state.sampleRates =
                uiDevices.makeSampleRateDropdown(currentDeviceId, config.sampleRate);
            state.blockSizes =
                uiDevices.makeBlockSizeDropdown(currentDeviceId, config.maxBlockSize);
        }

        lastInputPorts = host.midi.getInputPorts();
        state.midiPorts = uiMidi.makeInputPortToggleList();
        return state;
    }

    SynthHost host {[this](const MIDI::Event& event) { onMidiApplied(event); }};
    MS::UIDeviceManager uiDevices {host.manager};
    MS::UIMidiManager uiMidi {host.midi};
    MS::Vector<MS::MidiPortInfo> lastInputPorts;
};

} // namespace Api
