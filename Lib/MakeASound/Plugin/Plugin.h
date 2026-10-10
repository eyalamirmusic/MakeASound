#pragma once

#include "../Audio/Processor.h"
#include "Editor.h"
#include "HostEditListener.h"
#include "ParameterList.h"
#include "StateContext.h"

#include <string>
#include <string_view>
#include <utility>

namespace MakeASound
{

enum class PluginFormat
{
    Unknown,
    VST3,
    AU,
    Standalone
};

// A Processor with a host behind it: a name, a layout it negotiates, latency and
// tail, a parameter registry, a state document and an optional editor.
class Plugin : public Processor
{
public:
    Plugin() = default;

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    // Pure: a module may hold several plugin classes, so none of them can take
    // its name from describeModule(), and a placeholder would reach a window title.
    virtual std::string_view name() const = 0;

    // Set by the wrapper right after construction, so valid from prepare() on.
    PluginFormat format() const noexcept { return pluginFormat; }
    void setFormat(PluginFormat formatToUse) noexcept { pluginFormat = formatToUse; }

    // The default accepts the declared channel counts and nothing else.
    virtual bool acceptsLayout(const BusLayout& proposed) const;

    // At the prepared sample rate; hosts ask after prepare().
    virtual int latencySamples() const noexcept { return 0; }

    // How long the output rings after the input goes silent; 0 for none.
    virtual int tailSamples() const noexcept { return 0; }

    // True once each time latencySamples() moved on its own since the last ask.
    // Message thread.
    virtual bool takeLatencyChanged() noexcept { return false; }

    // Null means no editor of its own, and the host shows its generic parameter
    // page. Cheap by contract: a host may call it only to learn whether one
    // exists, so build the view lazily in Editor::view(). Message thread.
    virtual OwningPointer<Editor> createEditor() { return {}; }

    // The plugin's whole document, on the message thread.
    virtual std::string saveState(StateContext context);
    virtual void loadState(std::string_view data, StateContext context);

    // The same document from any thread, or empty when the plugin cannot make
    // one there, in which case the wrapper marshals to the message thread.
    virtual std::string saveStateWithoutMessageThread(StateContext context);

    // False when the snapshot above has fallen behind the plugin.
    virtual bool isStateSnapshotCurrent() const { return true; }

    // The parameters out of loadState's document and nothing else. They are
    // atomic, so the wrapper applies them on the host's thread.
    virtual void loadParameters(std::string_view data, StateContext context);

    // Everything loadParameters() leaves, on the message thread. The default
    // loads the whole document, for a plugin that cannot split it.
    virtual void loadStateExceptParameters(std::string_view data,
                                           StateContext context);

    const ParameterList& parameters() const noexcept { return parameterList; }

    // Message thread; null where there is no host.
    void setHostEditListener(HostEditListener* listener) noexcept
    { editListener = listener; }

    HostEditListener* hostEditListener() const noexcept { return editListener; }

    void notifyHostLatencyChanged() noexcept;
    void notifyHostParameterInfoChanged() noexcept;

protected:
    // Before the wrapper is built: it sizes itself to the list.
    void setParameters(ParameterList list) { parameterList = std::move(list); }

private:
    ParameterList parameterList;
    PluginFormat pluginFormat = PluginFormat::Unknown;
    HostEditListener* editListener = nullptr;
};

} // namespace MakeASound
