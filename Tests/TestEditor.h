#pragma once

// An editor the format and UI suites show in place of a plugin's own: settable
// size policy, a plain eacp view built on first use, and a record of what its
// host did to it.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace TestPlugins
{
using namespace MakeASound;

class TestEditor final : public Editor
{
public:
    TestEditor();
    ~TestEditor() override;

    eacp::Graphics::View& view() override;

    EditorSize initialSize() const override { return size; }
    EditorConstraints constraints() const override { return limits; }

    void onAttached() override;
    void onRemoved() override;

    EditorSize size;
    EditorConstraints limits;

    int attached = 0;
    int removed = 0;
    EditorHost* hostAtAttach = nullptr;
    EditorHost* hostAtRemove = nullptr;

private:
    OwningPointer<eacp::Graphics::View> content;
};

} // namespace TestPlugins
