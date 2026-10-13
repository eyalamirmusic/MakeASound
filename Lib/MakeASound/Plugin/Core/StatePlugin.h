#pragma once

#include "Plugin.h"
#include "../State/State.h"

#include <string>
#include <string_view>

namespace MakeASound
{

// A Plugin that owns a State: it registers the state's parameters in declared
// order and routes every save and load through the state, so a plugin gets
// persistence without overriding any of it. StateT is State<ParamsT> or a
// subclass reflecting more members beside `params`.
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
        auto scope = LoadScope {loading};
        state.deserialize(data, context);
    }

    void loadStateExceptParameters(std::string_view data,
                                   StateContext context) override
    {
        auto scope = LoadScope {loading};
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
        auto scope = LoadScope {loading};

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

private:
    // A load the host asked for marks nothing modified: the host knows.
    struct LoadScope
    {
        explicit LoadScope(bool& flagToUse)
            : flag(flagToUse)
            , previous(flagToUse)
        {
            flag = true;
        }

        ~LoadScope() { flag = previous; }

        LoadScope(const LoadScope&) = delete;
        LoadScope& operator=(const LoadScope&) = delete;

        bool& flag;
        bool previous;
    };

    void stateMarkedChanged()
    {
        state.publish();

        if (!loading)
            this->notifyHostStateChanged();
    }

    bool loading = false;

    // Last, so it binds to a constructed state and lets go before it.
    EA::Listener republish {state.stateChanged(),
                            [this] { stateMarkedChanged(); },
                            EA::Listener::Modes::TriggerOnEvent};
};

} // namespace MakeASound
