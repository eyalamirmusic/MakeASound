#include "EmbeddedEditor.h"
#include "Adapter.h"
#include "../UI/GenericEditor.h"

#include <eacp/Graphics/View/View.h>
#include <eacp/Graphics/Window/EmbeddedView.h>

#include <cmath>

namespace MakeASound::AUv3
{

namespace
{
OwningPointer<Editor> editorFor(Plugin& plugin)
{
    auto editor = plugin.createEditor();

    if (!editor)
        editor = EA::makeOwned<GenericEditor>(plugin);

    return editor;
}

int points(CGFloat value)
{
    return static_cast<int>(std::lround(value));
}

bool fitsSide(int editor, int configuration) noexcept
{
    return configuration == 0 || editor <= configuration;
}

NSIndexSet* fittingConfigurations(Plugin& plugin,
                                  NSArray<AUAudioUnitViewConfiguration*>* available)
{
    auto editor = editorFor(plugin);
    auto limits = editor->constraints();
    auto initial = editor->initialSize();
    auto* indices = [NSMutableIndexSet indexSet];

    for (NSUInteger index = 0; index < available.count; ++index)
    {
        auto* configuration = available[index];

        if (EmbeddedEditor::fits(
                limits,
                initial,
                {points(configuration.width), points(configuration.height)}))
            [indices addIndex:index];
    }

    return indices;
}
} // namespace

EmbeddedEditor::EmbeddedEditor(Adapter& adapter)
    : owner(&adapter)
    , editor(editorFor(adapter.plugin()))
{
}

EmbeddedEditor::~EmbeddedEditor()
{
    close();
}

EditorSize EmbeddedEditor::openingSize() const
{
    return editor ? editor->constraints().constrain(editor->initialSize())
                  : EditorSize {};
}

void EmbeddedEditor::attach(void* nativeView, EditorSize size, SizeRequest request)
{
    if (!editor || embedded)
        return;

    sizeRequest = std::move(request);

    embedded = EA::makeOwned<eacp::Graphics::EmbeddedView>(
        nativeView, eacp::Graphics::EmbeddedViewOptions {size.width, size.height});
    embedded->setContentView(editor->view());

    editor->setHost(this);
    editor->onAttached();

    // Our own lifetime bounds the capture: close() unregisters before we go.
    owner->addViewCloser(this, [this] { close(); });
}

void EmbeddedEditor::hostResized(EditorSize size)
{
    if (embedded)
        embedded->setSize(size.width, size.height);
}

void EmbeddedEditor::close()
{
    if (owner != nullptr)
    {
        owner->removeViewCloser(this);
        owner = nullptr;
    }

    if (editor && embedded)
    {
        editor->onRemoved();
        editor->setHost(nullptr);
    }

    embedded.reset();
    editor.reset();
    sizeRequest = nullptr;
}

bool EmbeddedEditor::resizeEditor(EditorSize size)
{
    return sizeRequest && sizeRequest(size);
}

bool EmbeddedEditor::fits(const EditorConstraints& limits,
                          EditorSize initial,
                          EditorSize configuration) noexcept
{
    auto smallest = limits.constrain(limits.resizable ? limits.minimum : initial);

    return fitsSide(smallest.width, configuration.width)
           && fitsSide(smallest.height, configuration.height);
}

NSIndexSet* EmbeddedEditor::supportedConfigurations(
    Plugin& plugin, NSArray<AUAudioUnitViewConfiguration*>* available)
{
    if (NSThread.isMainThread)
        return fittingConfigurations(plugin, available);

    __block NSIndexSet* result = nil;
    auto* pluginPtr = &plugin;

    dispatch_sync(dispatch_get_main_queue(), ^{
      result = fittingConfigurations(*pluginPtr, available);
    });

    return result;
}

} // namespace MakeASound::AUv3
