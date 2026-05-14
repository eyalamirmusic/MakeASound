#pragma once

#include "AudioProcessor.h"

#include <MakeASound/MakeASound.h>
#include <eacp/Core/Core.h>
#include <eacp/WebView/WebView.h>
#include <WebResources.h>

#include <string>
#include <utility>

namespace eg = eacp::Graphics;

struct SynthUIState
{
    MIRO_REFLECT(playing,
                 gain,
                 note,
                 frequency,
                 velocity,
                 devices,
                 sampleRates,
                 blockSizes,
                 midiPorts)

    bool playing {};
    double gain {};
    int note {-1};
    double frequency {};
    double velocity {};
    MS::UI::DropdownInfo devices;
    MS::UI::DropdownInfo sampleRates;
    MS::UI::DropdownInfo blockSizes;
    MS::UI::ToggleListInfo midiPorts;
};

struct MidiPortToggleRequest
{
    MIRO_REFLECT(id, on)

    int id {};
    bool on {};
};

struct SynthUI
{
    explicit SynthUI(AudioProcessor& processorToUse)
        : processor(processorToUse)
    {
        eg::setApplicationMenuBar(eg::buildDefaultWebViewMenuBar());

        registerCommands();
        window.setContentView(webView);

        processor.setMidiAppliedCallback(
            [this](const MIDI::Event& event) { publishMidi(event); });
    }

    void registerCommands()
    {
        auto& bridge = transport.getBridge();

        bridge.on<Miro::EmptyValue, SynthUIState>(
            "getState",
            std::function<SynthUIState(const Miro::EmptyValue&)>(
                [this](const auto&) { return makeState(); }));

        bindVoid<double>("setGain",
                         [this](double value)
                         {
                             processor.getSynth().setGain(
                                 static_cast<float>(value));
                         });

        bindVoid<int>("setSampleRate",
                      [this](int value) { processor.applySampleRate(value); });

        bindVoid<int>("setBlockSize",
                      [this](int value) { processor.applyBlockSize(value); });

        bindVoid<int>("setDevice",
                      [this](int id)
                      {
                          if (processor.applyDevice(id))
                              broadcastState();
                      });

        bindVoid<MidiPortToggleRequest>(
            "midiPortToggle",
            [this](const MidiPortToggleRequest& req)
            { processor.applyMidiPortToggle(req.id, req.on); });

        bindVoid<Miro::EmptyValue>(
            "allNotesOff",
            [this](const auto&) { processor.getSynth().releaseAllNotes(); });
    }

    template <typename Req>
    void bindVoid(const std::string& name, auto handler)
    {
        using Res = Miro::EmptyValue;
        transport.getBridge().on<Req, Res>(
            name,
            std::function<Res(const Req&)>(
                [handler = std::move(handler)](const Req& req) -> Res
                {
                    handler(req);
                    return {};
                }));
    }

    void publishMidi(const MIDI::Event& event)
    {
        auto& bridge = transport.getBridge();
        bridge.emit("midi", MIDI::toString(event));
        bridge.emit("audio", processor.getSynth().makeControls());
    }

    void pollMidiPorts()
    {
        auto current = processor.midi.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = std::move(current);
        transport.getBridge().emit("midiPorts", uiMidi.makeInputPortToggleList());
    }

    SynthUIState makeState()
    {
        auto controls = processor.getSynth().makeControls();
        auto& config = processor.getStreamConfig();

        auto state = SynthUIState {};
        state.playing = controls.playing;
        state.gain = controls.gain;
        state.note = controls.note;
        state.frequency = controls.frequency;
        state.velocity = controls.velocity;

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

    void broadcastState() { transport.getBridge().emit("state", makeState()); }

    AudioProcessor& processor;
    MS::UIDeviceManager uiDevices {processor.manager};
    MS::UIMidiManager uiMidi {processor.midi};
    MS::Vector<MS::MidiPortInfo> lastInputPorts;
    eg::WebView webView {eg::embeddedOptions("SynthWeb")};
    eg::WebViewBridge transport {webView};
    eg::Window window;
    eacp::Threads::Timer midiPollTimer {[this] { pollMidiPorts(); }, 2};
};
