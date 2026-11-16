#pragma once

// CLAP-only: includes the CLAP headers.

#include "../../Plugin/Parameters/Parameter.h"

#include <clap/ext/params.h>
#include <clap/plugin.h>

#include <atomic>
#include <string>

namespace MakeASound::Hosting
{

class CLAPHostedParameter;

// Where a host-side edit of a hosted CLAP parameter goes: the plugin's next
// block, or a flush when it is not active, the only ways a CLAP plugin hears of
// one. CLAPHostedPlugin implements it.
struct CLAPParameterSink
{
    virtual ~CLAPParameterSink() = default;

    // Message thread; the value, plain, is already in the parameter.
    virtual void parameterEdited(const CLAPHostedParameter& param,
                                 double plain) noexcept = 0;
};

// One parameter of a hosted CLAP plugin, in the plugin's own plain range: its
// minValue() and maxValue() are clap_param_info's, stepped (whole numbers, one
// step each) where the plugin says, and its text comes from the plugin's
// clap.params. Its id is the clap_id in decimal and its host id the clap_id.
//
// The value is kept as CLAP has it, a plain double, in a relaxed atomic both
// threads read: events to and from the plugin carry it untouched, and the
// float API of Parameter (getValue(), setValue(), and the normalized pair
// through Parameter's linear mapping) is a view onto it. The one conversion
// from a normalized value is plainFromNormalized(), in double.
class CLAPHostedParameter : public Parameter
{
public:
    CLAPHostedParameter(const clap_plugin& pluginToUse,
                        const clap_plugin_params& paramsToUse,
                        const clap_param_info& info,
                        double initialValue,
                        CLAPParameterSink& sinkToUse);

    clap_id paramId() const noexcept { return clapId; }

    // The plugin's cookie, handed back in every event that names the parameter.
    void* cookie() const noexcept { return clapCookie; }

    // The plugin publishes it, and a host's edit of it is dropped.
    bool isReadOnly() const noexcept { return readOnly; }

    float minValue() const noexcept override { return static_cast<float>(low); }
    float maxValue() const noexcept override { return static_cast<float>(high); }
    float defaultValue() const noexcept override
    {
        return static_cast<float>(initial);
    }
    int numSteps() const noexcept override { return steps; }
    bool isDiscrete() const noexcept override { return discrete; }
    bool isBoolean() const noexcept override { return boolean; }

    // A host-side edit, plain, off the audio thread: clamped, snapped when
    // stepped, kept, then handed to the sink.
    void setValue(float plain) noexcept override;
    float getValue() const noexcept override;

    double plainValue() const noexcept;

    // A normalized value (0..1, linear over the range) as the plain one the
    // plugin is sent: clamped, and snapped when stepped. Any thread.
    double plainFromNormalized(double normalized) const noexcept;

    // Clamped to the range and snapped when stepped; non-finite is the current.
    double clampPlain(double plain) const noexcept;

    // The plugin's own text for a value, and its parse of one (value_to_text /
    // text_to_value), falling back to Parameter's. Message thread.
    std::string valueToText(float plain) const override;
    float textToValue(std::string_view text) const override;

    // A value the plugin reported (an output event, a rescan, a state load):
    // kept, nothing sent. Any thread.
    void store(double plain) noexcept;

    // Re-reads the name after a rescan. True when it changed. The range, steps,
    // default and flags stay as first read: the audio thread reads those
    // unsynchronized, and a plugin that changes them must be reloaded. Message
    // thread.
    bool updateInfo(const clap_param_info& info);

private:
    const clap_plugin& plugin;
    const clap_plugin_params& params;
    CLAPParameterSink& sink;
    clap_id clapId;
    void* clapCookie;
    double low;
    double high;
    double initial;
    int steps;
    bool discrete;
    bool boolean;
    bool readOnly;
    std::atomic<double> value;
};

} // namespace MakeASound::Hosting
