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
        state.deserialize(data, context);
    }

    void loadStateExceptParameters(std::string_view data,
                                   StateContext context) override
    {
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

    StateT state;
    ParamsType& params = state.params;

private:
    // Last, so it binds to a constructed state and lets go before it.
    EA::Listener republish {state.stateChanged(),
                            [this] { state.publish(); },
                            EA::Listener::Modes::TriggerOnEvent};
};

} // namespace MakeASound
