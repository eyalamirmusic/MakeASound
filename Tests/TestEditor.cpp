#include "TestEditor.h"

#include <eacp/Graphics/View/View.h>

namespace TestPlugins
{

TestEditor::TestEditor() = default;

TestEditor::~TestEditor() = default;

eacp::Graphics::View& TestEditor::view()
{
    if (content == nullptr)
        content = EA::makeOwned<eacp::Graphics::View>();

    return *content;
}

void TestEditor::onAttached()
{
    ++attached;
    hostAtAttach = host();
}

void TestEditor::onRemoved()
{
    ++removed;
    hostAtRemove = host();
}

} // namespace TestPlugins
