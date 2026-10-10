#pragma once

#include "ParameterGroup.h"
#include "StateContext.h"

#include <Miro/Miro.h>
#include <ea_data_structures/Pointers/Broadcaster.h>

#include <concepts>
#include <mutex>
#include <string>
#include <string_view>

namespace MakeASound
{

// Whether the object being loaded holds an object under `key`.
inline bool holdsObjectAt(Miro::Reflector& ref, std::string_view key)
{
    auto options = ref.options();
    options.shape = Miro::Shape::Object;
    options.nullable = false;
    options.omittable = false;
    return ref.atKey(key, options).kind() == Miro::ValueKind::Object;
}

// A plugin's persistent state: `version` plus a `params` object holding the
// parameter group, and whatever a subclass reflects beside them.
//
// Loading is tolerant. A parameter the `params` object does not state goes back
// to its default, so a document older than the parameter still decides it; a
// document with no `params` object, or no JSON at all, moves nothing.
template <typename ParamsT>
    requires std::derived_from<ParamsT, ParameterGroup>
class State
{
public:
    using ParamsType = ParamsT;

    virtual ~State() = default;

    // The one hook: a subclass persisting more calls this, then reflects its
    // own members as siblings of `params`.
    virtual void reflect(Miro::Reflector& ref)
    {
        if (ref.isSaving())
        {
            ref["version"](version);
        }
        else
        {
            loadedVersion = version;
            ref["version"](loadedVersion);
        }

        if (skippingParams)
            return;

        if (ref.isSaving() || holdsObjectAt(ref, "params"))
            ref["params"](params);
    }

    std::string serialize(StateContext context = StateContext::Preset) const
    { return Miro::toJSONString(*this, 0, customOptionsFor(context)); }

    void deserialize(std::string_view data,
                     StateContext context = StateContext::Preset)
    {
        Miro::fromJSONString(*this, data, customOptionsFor(context));
        markChanged();
    }

    // The message-thread half of a load whose parameters loadParams() applied.
    void deserializeExceptParams(std::string_view data,
                                 StateContext context = StateContext::Preset)
    {
        skippingParams = true;
        Miro::fromJSONString(*this, data, customOptionsFor(context));
        skippingParams = false;
        markChanged();
    }

    // The atomic half of a load, safe from any thread: every parameter value
    // is an atomic, and nothing else is touched.
    void loadParams(std::string_view data,
                    StateContext context = StateContext::Preset)
    {
        auto document = Miro::Json::getParsedValue(data);

        if (!document.isObject())
            return;

        auto* section = Miro::Json::find(document.asObject(), "params");

        if (section != nullptr && section->isObject())
            Miro::fromJSON(params, *section, customOptionsFor(context));
    }

    // Hosts save on threads of their own, where waiting on the message thread
    // deadlocks (`auval -strict -stress`). So every non-parameter mutation ends in
    // markChanged(), the owner answers with publish(), and any thread reads.
    void markChanged() { changed.trigger(); }
    EA::Broadcaster& stateChanged() noexcept { return changed; }

    // Message thread.
    void publish()
    {
        auto preset = serialize(StateContext::Preset);
        auto session = serialize(StateContext::Session);
        auto lock = std::lock_guard {publishedMutex};
        publishedPreset = std::move(preset);
        publishedSession = std::move(session);
    }

    std::string publishedDocument(StateContext context = StateContext::Session) const
    {
        auto lock = std::lock_guard {publishedMutex};
        return context == StateContext::Session ? publishedSession : publishedPreset;
    }

    // Any thread: the published document with the parameters read live, since
    // they are the half that moves off the message thread.
    std::string snapshotDocument(StateContext context = StateContext::Session) const
    {
        auto document = Miro::Json::getParsedValue(publishedDocument(context));

        if (!document.isObject())
            document = Miro::Json::Value {Miro::Json::Object {}};

        document.asObject()["params"] =
            Miro::toJSON(params, customOptionsFor(context));
        return Miro::Json::print(document);
    }

    // False when something besides the parameters changed without a publish.
    // Parameters are left out: they move under automation between any two reads.
    bool isPublishedDocumentCurrent() const
    {
        for (auto context: {StateContext::Preset, StateContext::Session})
            if (withoutParams(serialize(context))
                != withoutParams(publishedDocument(context)))
                return false;

        return true;
    }

    ParamsT params;

    // The schema this build writes. Loading leaves it alone and reads the
    // document's into loadedVersion, for migration under ref.isLoading().
    int version = 1;
    int loadedVersion = 1;

private:
    static std::string withoutParams(const std::string& data)
    {
        auto document = Miro::Json::getParsedValue(data);

        if (document.isObject())
            document.asObject().erase("params");

        return Miro::Json::print(document);
    }

    EA::Broadcaster changed;
    bool skippingParams = false;

    // Held across a string copy, never across a serialize.
    mutable std::mutex publishedMutex;
    std::string publishedPreset;
    std::string publishedSession;
};

} // namespace MakeASound
