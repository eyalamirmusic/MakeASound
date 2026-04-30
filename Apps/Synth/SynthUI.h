#pragma once

#include "AudioProcessor.h"

#include <MakeASound/MakeASound.h>
#include <eacp/Core/Core.h>
#include <eacp/WebView/WebView.h>
#include <WebResources.h>

struct SynthUIState
{
    MIRO_REFLECT(playing,
                 gain,
                 note,
                 frequency,
                 velocity,
                 blockSize,
                 devices,
                 sampleRates,
                 midiPorts)

    bool playing {};
    double gain {};
    int note {-1};
    double frequency {};
    double velocity {};
    int blockSize {};
    MS::UI::DropdownInfo devices;
    MS::UI::DropdownInfo sampleRates;
    MS::UI::ToggleListInfo midiPorts;
};

struct SynthUI
{
    explicit SynthUI(AudioProcessor& processorToUse)
        : processor(processorToUse)
    {
        webView.addScriptMessageHandler(
            "synth", [this](const std::string& msg) { handleMessage(msg); });

        window.setContentView(webView);

        processor.setMidiAppliedCallback(
            [this](const MIDI::Event& event)
            { publishMidiToJS(event); });
    }

    void handleMessage(const std::string& jsonStr)
    {
        auto json = Miro::Json::parse(jsonStr);

        if (!json.isObject())
            return;

        auto& obj = json.asObject();
        auto* type = Miro::Json::find(obj, "type");

        if (type == nullptr || !type->isString())
            return;

        auto kind = type->asString();

        if (kind == "ready")
            sendState();
        else if (kind == "gain")
            processor.getSynth().setGain(obj["value"]);
        else if (kind == "sampleRate")
            processor.applySampleRate(obj["value"]);
        else if (kind == "blockSize")
            processor.applyBlockSize(obj["value"]);
        else if (kind == "device")
        {
            if (processor.applyDevice(obj["id"]))
                sendState();
        }
        else if (kind == "midiPortToggle")
            processor.applyMidiPortToggle(obj["id"], obj["on"]);
        else if (kind == "allNotesOff")
            processor.getSynth().releaseAllNotes();
    }

    void sendState()
    {
        auto controls = processor.getSynth().makeControls();
        auto& config = processor.getStreamConfig();

        auto state = SynthUIState {};
        state.playing = controls.playing;
        state.gain = controls.gain;
        state.note = controls.note;
        state.frequency = controls.frequency;
        state.velocity = controls.velocity;
        state.blockSize = config.maxBlockSize;

        auto currentDeviceId = config.output ? config.output->device.id : 0;
        state.devices = uiDevices.makeOutputDeviceDropdown(currentDeviceId);

        if (config.output)
            state.sampleRates =
                uiDevices.makeSampleRateDropdown(currentDeviceId, config.sampleRate);

        lastInputPorts = processor.midi.getInputPorts();
        state.midiPorts = uiMidi.makeInputPortToggleList();

        webView.evaluateJavaScript("window.synthSetState("
                                   + Miro::toJSONString(state) + ");");
    }

    void publishMidiToJS(const MIDI::Event& event)
    {
        auto text = MIDI::toString(event);
        webView.evaluateJavaScript("window.synthMidiEvent("
                                   + Miro::toJSONString(text) + ");");

        webView.evaluateJavaScript(
            "window.synthUpdateAudio("
            + Miro::toJSONString(processor.getSynth().makeControls()) + ");");
    }

    void pollMidiPorts()
    {
        auto current = processor.midi.getInputPorts();

        if (current == lastInputPorts)
            return;

        lastInputPorts = std::move(current);

        auto info = uiMidi.makeInputPortToggleList();
        webView.evaluateJavaScript("window.synthSetMidiPorts("
                                   + Miro::toJSONString(info) + ");");
    }

    AudioProcessor& processor;
    MS::UIDeviceManager uiDevices {processor.manager};
    MS::UIMidiManager uiMidi {processor.midi};
    MS::Vector<MS::MidiPortInfo> lastInputPorts;
    eacp::Graphics::WebView webView {eacp::Graphics::embeddedOptions("SynthWeb")};
    eacp::Graphics::Window window;
    eacp::Threads::Timer midiPollTimer {[this] { pollMidiPorts(); }, 2};
};
