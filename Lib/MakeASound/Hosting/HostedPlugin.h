#pragma once

#include "PluginDescription.h"
#include "../Audio/Playhead.h"

#include <atomic>
#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// What a host tells the plugins it loads about itself (VST3's IHostApplication
// name).
struct HostInfo
{
    std::string name = "MakeASound Host";
};

// A third-party plugin, loaded and running in this process: a MakeASound::Plugin,
// so a Processor, so an app runs it with engine.start(config, hosted) and a
// plugin that hosts calls its process() from its own. Never wrapped in a
// PluginWrapper: that adapts OUR plugins to a host, and its added bypass and id
// derivation are wrong for a foreign plugin's parameters.
//
// What Plugin already says holds unchanged: process() on the audio thread,
// allocation-free; prepare(), layouts, saveState/loadState (the plugin's own
// opaque chunk) and createEditor() on the message thread; and the plugin's GUI
// reaches the host as begin/perform/end through the HostEditListener slot, on
// the message thread. parameters() lists the plugin's parameters in its order,
// each with the plugin's own id as its host id (hostedPluginIdentity()) and that
// id in decimal as its id.
//
// The rest is here: the hooks only a host needs.
class HostedPlugin : public Plugin
{
public:
    explicit HostedPlugin(PluginDescription descriptionToUse);

    // The scanned description it was loaded from.
    const PluginDescription& description() const noexcept { return described; }

    std::string_view name() const override { return described.name; }

    // A hosted plugin's parameters are its own; its bypass, when it has one, is
    // one of them.
    bool wantsHostBypass() const override { return false; }

    // Audio thread, lock-free and allocation-free: a value for the next block,
    // as a host's automation. False when the index is out of range or the plugin
    // is not prepared.
    virtual bool setParameterFromAudioThread(int index,
                                             float normalized) noexcept = 0;

    // Message thread: pushes values the plugin's processor reported to its own
    // editor, which cannot be told from the audio thread. Cheap when nothing
    // changed; call it from a UI timer. Nothing breaks if it is never called,
    // but the plugin's GUI goes stale.
    virtual void syncEditorWithProcessor() = 0;

    // Message thread: re-reads one parameter's value from the plugin, for a
    // format whose editor can move a value without telling the host (an Audio
    // Unit's). A no-op where the format always tells.
    virtual void refreshParameterValue(int index);

    // Once each time the plugin renamed parameters, already re-read into
    // parameters() (names only; ids, order and ranges stay).
    bool takeParameterInfoChanged() noexcept;

    // Once each time the plugin changed its buses: the host must renegotiate the
    // layout and prepare again.
    bool takeIoChanged() noexcept;

    bool takeLatencyChanged() noexcept override;

    // The transport the plugin sees in a block whose ProcessContext has no valid
    // playhead (an app with no transport). Defaults to stopped at 120 BPM in 4/4;
    // its sample position advances only while it says it is playing. Set it while
    // the plugin is not processing.
    void setFallbackPlayhead(const Playhead& playheadToUse) noexcept;
    const Playhead& fallbackPlayhead() const noexcept { return fallback; }

protected:
    // For the format, from whatever thread the plugin told it on.
    void raiseParameterInfoChanged() noexcept;
    void raiseIoChanged() noexcept;
    void raiseLatencyChanged() noexcept;

    // The playhead for a block: the context's when valid, else the fallback at
    // its own position, the musical one derived from the samples at its tempo,
    // which then advances by the block while playing. Audio thread.
    Playhead playheadFor(const Playhead& supplied,
                         int numSamples,
                         double sampleRate) noexcept;

    // Starts the fallback's position over, for prepare().
    void rewindFallbackPlayhead() noexcept { fallbackSampleTime = 0; }

private:
    PluginDescription described;
    Playhead fallback;
    int64_t fallbackSampleTime = 0;

    std::atomic<bool> parameterInfoChanged {false};
    std::atomic<bool> ioChanged {false};
    std::atomic<bool> latencyChanged {false};
};

// What loading a plugin produced: the plugin, or why there is none.
struct LoadResult
{
    OwningPointer<HostedPlugin> plugin;
    std::string error;
};

} // namespace MakeASound::Hosting
