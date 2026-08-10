#pragma once

#include <Miro/Miro.h>

#include "../Common/Common.h"
#include "../Audio/Buffer.h"

#include <optional>
#include <string>
#include <functional>

namespace MakeASound
{

struct DeviceInfo
{
    // Whether this names a device that exists. Enumeration hands back a blank
    // DeviceInfo when the machine has nothing to offer, so anything that builds a
    // config out of a default device has to ask before using one.
    bool isValid() const;

    // The same question for one direction. A desktop with speakers and no microphone
    // is the common case, and asking such a machine for a duplex stream opens
    // neither side — the whole open fails on the half that isn't there.
    bool hasChannels(bool input) const;

    MIRO_REFLECT(id,
                 name,
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
    int outputChannels {};
    int inputChannels {};
    int duplexChannels {};
    bool isDefaultOutput {false};
    bool isDefaultInput {false};
    Vector<int> sampleRates;

    // What the device is running at now, as opposed to what it *can* run
    // (sampleRates) or what we would open it at (preferredSampleRate). A shared
    // device moves whenever another app moves it, so this is a snapshot from the
    // moment it was enumerated, not a property of the device. Falls back to
    // preferredSampleRate where the platform can't be asked.
    int currentSampleRate {};
    int preferredSampleRate {};
};

// Why an operation didn't work. Returned rather than thrown: a machine with no audio
// device, or one whose device is busy, is an ordinary state on the desktop and not
// something a host should have to catch to survive.
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

// A message fit to put in front of a user. Empty for NoError — every host that can
// fail to open a device needs something to show, and the enumerator names are not it.
std::string getErrorMessage(Error error);

int getDefaultNumChannels(const DeviceInfo& info, bool input);
bool deviceSupportsSampleRate(const DeviceInfo& device, int rate);

// Pick a sample rate both devices can drive. Prefers the output's preferred
// rate, then the input's preferred rate, then the highest rate present in
// both lists, with output-only fallbacks if no common rate exists.
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

    // A stream uses the contiguous span of a device's channels starting at
    // firstChannel: [firstChannel, firstChannel + nChannels). This is how a
    // specific input/output (e.g. a stereo pair or a single channel) is picked
    // out of a multi-channel device such as an audio interface.
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

// Something the device did on its own, outside any call the host made — the OS
// stopped it, moved it to different hardware, or interrupted it. Deliberate stops
// made through this library are NOT reported: this means the device, not the host.
//
// Purely informational. A Stopped device is re-opened automatically (see
// DeviceManager::setAutoRecover), so a host that never looks at these keeps getting
// audio across a sample-rate change made by another app, an unplug, or the OS
// reclaiming the device. Listen only if you want to know it happened.
//
// Nothing else reveals a Stopped device: no further audio callbacks arrive AND it
// still reports itself as started, so polling says everything is fine.
//
// Not every backend posts every type; some reroute silently. See
// DeviceManager::setNotificationCallback.
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
    // Planar (non-interleaved) views over this block's channels. The backend
    // owns the interleaved<->planar conversion, so callers only ever see planar
    // data through these.
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
    // the first callback after the device notified us of a change — a reroute or a
    // resumed interruption can hand back audio from different hardware without the
    // shape moving. That second case is reported here whether or not the host
    // registered a notification callback, so a host that only implements the audio
    // callback still learns the stream is no longer the one it was.
    bool dirty = false;
    int errorCode = 0;
};

using Callback = std::function<void(AudioCallbackInfo&)>;
using NotificationCallback = std::function<void(DeviceNotification)>;
} // namespace MakeASound
