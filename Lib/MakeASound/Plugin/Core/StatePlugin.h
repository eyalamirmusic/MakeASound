#pragma once

#include "Plugin.h"
#include "../State/State.h"

#include <string>
#include <string_view>
#include <utility>

namespace MakeASound
{

// A Plugin that owns a State: it registers the state's parameters in declared
// order and routes every save and load through the state, so a plugin gets
// persistence without overriding any of it. StateT is State<ParamsT> or a
// subclass reflecting more members beside `params`.
//
// Every state.markChanged() republishes the snapshot and tells the host its
// document is modified, unless a QuietStateChanges is open (Plugin.h): then the
// host is not told, and the snapshot is published once, when the outermost
// scope closes. Loads open one here, and the wrapper opens one around each
// load and message-thread save, overrides included.
template <class StateT>
class StatePlugin : public Plugin
{
public:
    using ParamsType = typename StateT::ParamsType;

    StatePlugin()
    {
        setParameters(ParameterList {state.params});

        // Here rather than in State: reflect() is virtual, and a base
        // constructor would publish without the subclass's members.
        state.publish();
    }

    std::string saveState(StateContext context) override
    {
        return state.serialize(context);
    }

    void loadState(std::string_view data, StateContext context) override
    {
        auto quiet = quietStateChanges();
        state.deserialize(data, context);
    }

    void loadStateExceptParameters(std::string_view data,
                                   StateContext context) override
    {
        auto quiet = quietStateChanges();
        state.deserializeExceptParams(data, context);
    }

    std::string saveStateWithoutMessageThread(StateContext context) override
    {
        return state.snapshotDocument(context);
    }

    bool isStateSnapshotCurrent() const override
    {
        return state.isPublishedDocumentCurrent();
    }

    void loadParameters(std::string_view data, StateContext context) override
    {
        state.loadParams(data, context);
    }

    // Routed to loadLegacy(), then the state republished. A load the host asked
    // for, so the host is not told the document changed, as for a native load.
    bool loadForeignState(const ForeignState& foreign) override
    {
        auto quiet = quietStateChanges();

        if (!loadLegacy(foreign))
            return false;

        state.markChanged();
        return true;
    }

    StateT state;
    ParamsType& params = state.params;

protected:
    // A port's reader of what its earlier framework saved: `params` and whatever
    // else `state` holds, set from `foreign.bytes` (Compat/ has the readers),
    // true when it took them. Message thread. The default takes nothing.
    virtual bool loadLegacy(const ForeignState&) { return false; }

    // An override calls this first.
    void quietStateChangesEnded() override
    {
        if (std::exchange(publishPending, false))
            state.publish();
    }

private:
    void stateMarkedChanged()
    {
        if (stateChangesAreQuiet())
        {
            publishPending = true;
            return;
        }

        state.publish();
        this->notifyHostStateChanged();
    }

    bool publishPending = false;

    // Last, so it binds to a constructed state and lets go before it.
    EA::Listener republish {state.stateChanged(),
                            [this] { stateMarkedChanged(); },
                            EA::Listener::Modes::TriggerOnEvent};
};

} // namespace MakeASound
