#pragma once

#include "../Core/Editor.h"
#include "../Core/Plugin.h"
#include "../../Common/Common.h"

namespace MakeASound
{

// The page a host shows for a plugin whose createEditor() returns null: one row
// per parameter, a label, a control by type and the value text, in eacp's
// widget tier. Every member runs on the message thread.
class GenericEditor : public Editor
{
public:
    explicit GenericEditor(Plugin& pluginToUse);
    ~GenericEditor() override;

    eacp::Graphics::View& view() override;

    EditorSize initialSize() const override;
    bool isResizable() const override { return true; }

    // The values are pulled from the parameters at 30 Hz while attached.
    void onAttached() override;
    void onRemoved() override;

private:
    struct Page;

    Plugin& plugin;
    OwningPointer<Page> page;
};

} // namespace MakeASound
