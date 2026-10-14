#pragma once

#include "../Core/Plugin.h"
#include "../../Common/Common.h"

#include <Miro/Miro.h>

#include <string>
#include <string_view>

namespace MakeASound::Web
{

// One entry of Plugin::parameters() as a page sees it. `id` is the id path every
// command takes ("Filter/Cutoff"; "Dry//Wet" for the id "Dry/Wet", see
// IdPath.h). `key` is the parameter's own id and `groups` its enclosing named
// groups' own ids, outermost first, so a page never parses the path. The default
// is the one resetParam() and resetAll() restore, normalized and as its readout.
struct WebParameter
{
    std::string id;
    std::string key;
    Vector<std::string> groups;
    std::string name;
    std::string label;
    double normalized = 0.0;
    std::string text;
    double defaultNormalized = 0.0;
    std::string defaultText;
    int steps = 0;
    Vector<std::string> choices;
    bool automatable = true;

    MIRO_REFLECT(id,
                 key,
                 groups,
                 name,
                 label,
                 normalized,
                 text,
                 defaultNormalized,
                 defaultText,
                 steps,
                 choices,
                 automatable)
};

// The schema codegen names reflected structs only, so every list and every bare
// id travels inside one.
struct WebParameters
{
    Vector<WebParameter> parameters;

    MIRO_REFLECT(parameters)
};

struct WebParameterId
{
    std::string id;

    MIRO_REFLECT(id)
};

struct WebParameterEdit
{
    std::string id;
    double normalized = 0.0;

    MIRO_REFLECT(id, normalized)
};

// Text a user typed for a parameter, read through its textToValue().
struct WebParameterText
{
    std::string id;
    std::string text;

    MIRO_REFLECT(id, text)
};

struct WebParameterValue
{
    std::string id;
    double normalized = 0.0;
    std::string text;

    MIRO_REFLECT(id, normalized, text)
};

struct WebParameterValues
{
    Vector<WebParameterValue> changed;

    MIRO_REFLECT(changed)
};

// Every entry's value, in list order: the payload of the keyed
// `parameterValues` event and of getParameterValues().
struct WebParameterSnapshot
{
    Vector<WebParameterValue> items;

    MIRO_REFLECT(items)
};

// The Miro API a page drives a plugin's parameters through, by id. Edits write
// the Parameter and report the gesture through the plugin's HostEditListener,
// as GenericEditor does; an edit outside beginEdit/endEdit is a whole gesture,
// and so is each change editText, resetParam and resetAll make. Default-
// constructible with no plugin for the schema codegen. Message thread.
//
// Two events carry what moved, whoever moved it. `values` holds only the
// entries that moved, for a page that merges them itself. `parameterValues`
// holds every entry, keyed by id, so the generated hooks give each control its
// own store: useWebParameterValue(id) re-renders only when that entry changes.
class ParameterApi
{
public:
    ParameterApi() = default;
    explicit ParameterApi(Plugin& pluginToUse);

    void reflect(Miro::ApiReflector& r)
    {
        using T = ParameterApi;

        r.commands<&T::getParameters,
                   &T::getParameterValues,
                   &T::beginEdit,
                   &T::edit,
                   &T::endEdit,
                   &T::editText,
                   &T::resetParam,
                   &T::resetAll>();

        r.events<&T::values>();
        r.keyedEvent(&T::parameterValues, "parameterValues", "items", "id");
    }

    WebParameters getParameters();
    WebParameterSnapshot getParameterValues();

    void beginEdit(const WebParameterId& param);
    void edit(const WebParameterEdit& change);
    void endEdit(const WebParameterId& param);

    // Sets the parameter to what its textToValue() reads from the text, which
    // for text that holds no value is the default.
    void editText(const WebParameterText& entry);

    // Restores the parameter's default.
    void resetParam(const WebParameterId& param);

    // Restores every entry's default but the host-facing bypass, one gesture
    // each, skipping the entries already there.
    void resetAll();

    // Ends every gesture the page left open, e.g. when its view goes.
    void endAllEdits();

    // Publishes the entries whose normalized value moved since the last
    // publish, whoever moved them, and nothing when none did.
    void poll();

    Miro::Event<WebParameterValues> values;
    Miro::Event<WebParameterSnapshot> parameterValues;

private:
    int indexOf(std::string_view id) const;
    WebParameterValue valueOf(int index) const;
    WebParameterSnapshot snapshot() const;

    // Writes the parameter and reports it, inside the open gesture or as a
    // whole one.
    void perform(int index, float normalized);

    void begin(int index);
    void end(int index);

    struct Slot
    {
        float published = 0.f;
        bool editing = false;
    };

    Plugin* plugin = nullptr;
    Vector<Slot> slots;
};

} // namespace MakeASound::Web
