#pragma once

#include <Miro/Miro.h>

#include "../Common/Common.h"
#include "../Audio/Buffer.h"

#include <optional>
#include <string>
#include <string_view>
#include <functional>

namespace MakeASound
{

// The system audio API a device is reached through. The same hardware appears
// under each API the machine offers, with different latency and channel counts;
// Unknown means "let the backend pick".
enum class Backend
{
    Unknown,
    WASAPI,
    DirectSound,
    WinMM,
    CoreAudio,
    SndIO,
    Audio4,
    OSS,
    PulseAudio,
    ALSA,
    JACK,
    AAudio,
    OpenSL,
    WebAudio,
    Null
};

// Display spelling — "Core Audio", not "CoreAudio". Empty for Backend::Unknown.
std::string getBackendName(Backend backend);

// Matches both spellings — getBackendName's and the enumerator's — ignoring case,
// spaces and punctuation: "Core Audio", "coreaudio" and "core-audio" all match.
std::optional<Backend> getBackendFromName(std::string_view name);

struct DeviceInfo
{
    // Enumeration hands back a blank DeviceInfo when the machine has nothing to
    // offer, so anything building a config out of a default device has to ask first.
    bool isValid() const;

    // Asking a machine with speakers but no microphone for a duplex stream opens
    // neither side — the whole open fails on the half that isn't there.
    bool hasChannels(bool input) const;

    MIRO_REFLECT(id,
                 name,
                 backend,
                 outputChannels,
                 inputChannels,
                 duplexChannels,
                 isDefaultOutput,
                 isDefaultInput,
                 sampleRates,
                 currentSampleRate,
                 preferredSampleRate)

    int id {};
    std::string name;

    // Ids are handed out per-API, so a DeviceInfo only means anything to the manager
    // that enumerated it — see DeviceManager::setBackend.
    Backend backend {Backend::Unknown};

    int outputChannels {};
    int inputChannels {};
    int duplexChannels {};
    bool isDefaultOutput {false};
    bool isDefaultInput {false};
    Vector<int> sampleRates;

    // What the device runs at now, not what it can run (sampleRates) or what we
    // would open it at (preferredSampleRate). A shared device moves whenever another
    // app moves it, so this is a snapshot from enumeration; falls back to preferred.
    int currentSampleRate {};
    int preferredSampleRate {};
};

// Returned rather than thrown: a machine with no audio device, or one whose device
// is busy, is an ordinary desktop state, not something a host should have to catch.
enum class Error
{
    NoError,
    WARNING,
    UNKNOWN_ERROR,
    NO_DEVICES_FOUND,
    INVALID_DEVICE,
    DEVICE_DISCONNECT,
    MEMORY_ERROR,
    INVALID_PARAMETER,
    INVALID_USE,
    DRIVER_ERROR,
    SYSTEM_ERROR,
    THREAD_ERROR
};

// A message fit to put in front of a user. Empty for NoError.
std::string getErrorMessage(Error error);

int getDefaultNumChannels(const DeviceInfo& info, bool input);
bool deviceSupportsSampleRate(const DeviceInfo& device, int rate);

// Prefers the output's preferred rate, then the input's, then the highest common
// rate, then output-only and input-only fallbacks.
int pickCompatibleSampleRate(const DeviceInfo& output, const DeviceInfo& input);

struct StreamParameters
{
    StreamParameters() = default;
    StreamParameters(const DeviceInfo& deviceToUse,
                     bool input,
                     int numChannels = -1,
                     int firstCh = 0);

    MIRO_REFLECT(device, nChannels, firstChannel)

    DeviceInfo device;

    // Uses the device's channels [firstChannel, firstChannel + nChannels), which is
    // how a stereo pair is picked out of a multi-channel interface.
    int nChannels {};
    int firstChannel {};
};

struct Flags
{
    MIRO_REFLECT(nonInterleaved, minimizeLatency, hogDevice)

    bool nonInterleaved = true;
    bool minimizeLatency = false;
    bool hogDevice = false;
};

struct StreamOptions
{
    MIRO_REFLECT(flags, numberOfBuffers, streamName, priority)

    Flags flags {};
    int numberOfBuffers {};
    std::string streamName {};
    int priority {};
};

enum class AudioCallbackStatus
{
    OK,
    InputOverflow,
    OutputUnderflow
};

// Something the device did on its own; stops made through this library are not
// reported. Informational only — a Stopped device is re-opened automatically (see
// setAutoRecover), and nothing else reveals one: it still reports itself started.
enum class DeviceNotification
{
    Started,
    Stopped,
    Rerouted,
    InterruptionBegan,
    InterruptionEnded,
    Unlocked
};

int getNumChannels(const std::optional<StreamParameters>& params);

struct StreamConfig
{
    int getInputChannels() const;
    int getOutputChannels() const;

    MIRO_REFLECT(input, output, sampleRate, maxBlockSize, options)

    std::optional<StreamParameters> input;
    std::optional<StreamParameters> output;

    int sampleRate {};
    int maxBlockSize = 0;
    std::optional<StreamOptions> options;
};

struct AudioCallbackInfo
{
    // Planar views; the backend owns the interleaved<->planar conversion, so
    // callers only ever see planar data.
    Buffer getInput() const;
    Buffer getOutput();

    bool operator==(const AudioCallbackInfo& other) const;
    bool operator!=(const AudioCallbackInfo& other) const;

    int numInputs = 0;
    int numOutputs = 0;
    float* outputBuffer = nullptr;
    float* inputBuffer = nullptr;
    int numSamples {};
    double streamTime {};
    AudioCallbackStatus status = AudioCallbackStatus::OK;

    int sampleRate = 0;
    int maxBlockSize = 0;
    int latency = 0;

    // "Re-derive whatever you cached about this stream." Raised when the shape
    // (channels, sample rate, block size) differs from the previous callback, and on
    // the first callback after a reroute or interruption, which leave it unchanged.
    bool dirty = false;
    int errorCode = 0;
};

using Callback = std::function<void(AudioCallbackInfo&)>;
using NotificationCallback = std::function<void(DeviceNotification)>;
} // namespace MakeASound
