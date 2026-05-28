#pragma once

#include "AudioProcessor.h"

#include <MakeASound/MakeASound.h>
#include <Miro/Miro.h>
#include <eacp/Core/Core.h>

#include <utility>

struct UIState
{
    MIRO_REFLECT(devices, sampleRates, blockSizes, midiPorts)

    MakeASound::UI::DropdownInfo devices;
    MakeASound::UI::DropdownInfo sampleRates;
    MakeASound::UI::DropdownInfo blockSizes;
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

class SynthApi
{
public:
    SynthApi()
    {
        processor.setMidiAppliedCallback(
            [this](const MIDI::Event& event) { onMidiApplied(event); });
    }

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
    AudioControls getAudio() const { return processor.getSynth().makeControls(); }

    void setGain(const double& value)
    {
        processor.getSynth().setGain(static_cast<float>(value));
        audio.publish(processor.getSynth().makeControls());
    }

    void setSampleRate(const int& value)
    {
        processor.applySampleRate(value);
        ui.publish(makeUi());
    }

    void setBlockSize(const int& value)
    {
        processor.applyBlockSize(value);
        ui.publish(makeUi());
    }

    void setDevice(const int& id)
    {
        if (processor.applyDevice(id))
            ui.publish(makeUi());
    }

    void midiPortToggle(const MidiPortToggleRequest& req)
    {
        processor.applyMidiPortToggle(req.id, req.on);
        ui.publish(makeUi());
    }

    void allNotesOff() { processor.getSynth().releaseAllNotes(); }

    void pollMidiPorts()
    {
        auto current = processor.midi.getInputPorts();

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
        audio.publish(processor.getSynth().makeControls());
    }

    UIState makeUi()
    {
        auto& config = processor.getStreamConfig();
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

        lastInputPorts = processor.midi.getInputPorts();
        state.midiPorts = uiMidi.makeInputPortToggleList();
        return state;
    }

    AudioProcessor processor;
    MS::UIDeviceManager uiDevices {processor.manager};
    MS::UIMidiManager uiMidi {processor.midi};
    MS::Vector<MS::MidiPortInfo> lastInputPorts;
};

} // namespace Api
