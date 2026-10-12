#pragma once

#include "../Core/Plugin.h"
#include "../../Common/Common.h"

#include <Miro/Miro.h>

#include <string>
#include <string_view>

namespace MakeASound::Web
{

// One entry of Plugin::parameters() as a page sees it.
struct WebParameter
{
    std::string id;
    std::string name;
    std::string label;
    double normalized = 0.0;
    std::string text;
    int steps = 0;
    Vector<std::string> choices;
    bool automatable = true;

    MIRO_REFLECT(id, name, label, normalized, text, steps, choices, automatable)
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

// The Miro API a page drives a plugin's parameters through, by id. Edits write
// the Parameter and report the gesture through the plugin's HostEditListener,
// as GenericEditor does; an edit outside beginEdit/endEdit is a whole gesture.
// Default-constructible with no plugin for the schema codegen. Message thread.
class ParameterApi
{
public:
    ParameterApi() = default;
    explicit ParameterApi(Plugin& pluginToUse);

    void reflect(Miro::ApiReflector& r)
    {
        using T = ParameterApi;

        r.commands<&T::getParameters, &T::beginEdit, &T::edit, &T::endEdit>();
        r.events<&T::values>();
    }

    WebParameters getParameters();

    void beginEdit(const WebParameterId& param);
    void edit(const WebParameterEdit& change);
    void endEdit(const WebParameterId& param);

    // Ends every gesture the page left open, e.g. when its view goes.
    void endAllEdits();

    // Publishes the entries whose normalized value moved since the last
    // publish, whoever moved them, and nothing when none did.
    void poll();

    Miro::Event<WebParameterValues> values;

private:
    int indexOf(std::string_view id) const;
    WebParameterValue valueOf(int index) const;

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
