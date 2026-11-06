#pragma once

// An editor whose record outlives it, for the view suites that check what a
// format did to an editor it has already destroyed: the AU view's and the AUv3
// view controller's. The test plugins show one while a ScopedProbe lives.

#include "TestPlugins.h"

#include <eacp/Graphics/View/View.h>

namespace TestPlugins
{

// What happened to a ProbeEditor, readable after the view destroyed it.
struct EditorRecord
{
    EditorSize size {400, 300};
    EditorConstraints limits;

    Editor* editor = nullptr;
    eacp::Graphics::View* content = nullptr;
    EditorHost* hostAtAttach = nullptr;
    EditorHost* hostAtRemove = nullptr;
    int attached = 0;
    int removed = 0;
    bool destroyed = false;
};

class ProbeEditor final : public Editor
{
public:
    explicit ProbeEditor(EditorRecord& recordToUse)
        : record(recordToUse)
    {
        record.editor = this;
    }

    ~ProbeEditor() override
    {
        record.editor = nullptr;
        record.content = nullptr;
        record.destroyed = true;
    }

    eacp::Graphics::View& view() override
    {
        if (content == nullptr)
        {
            content = EA::makeOwned<eacp::Graphics::View>();
            record.content = content.get();
        }

        return *content;
    }

    EditorSize initialSize() const override { return record.size; }
    EditorConstraints constraints() const override { return record.limits; }

    void onAttached() override
    {
        ++record.attached;
        record.hostAtAttach = host();
    }

    void onRemoved() override
    {
        ++record.removed;
        record.hostAtRemove = host();
    }

private:
    EditorRecord& record;
    OwningPointer<eacp::Graphics::View> content;
};

// The test plugins show a ProbeEditor over the record while this lives.
struct ScopedProbe
{
    ScopedProbe()
    {
        Module::editorFactory = [this](Plugin&)
        { return OwningPointer<Editor>(EA::makeOwned<ProbeEditor>(record)); };
    }

    ~ScopedProbe() { Module::editorFactory = nullptr; }

    ScopedProbe(const ScopedProbe&) = delete;
    ScopedProbe& operator=(const ScopedProbe&) = delete;

    EditorRecord record;
};

inline EditorConstraints resizableBetween(EditorSize minimum, EditorSize maximum)
{
    return {.resizable = true, .minimum = minimum, .maximum = maximum};
}

} // namespace TestPlugins
