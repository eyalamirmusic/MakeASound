#include "InstrumentEditor.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Graphics/View/View.h>

#include <memory>

namespace HostFixtures
{

namespace
{
using namespace MakeASound;

class FixedSizeEditor final : public Editor
{
public:
    ~FixedSizeEditor() override { *alive = false; }

    eacp::Graphics::View& view() override
    {
        if (content == nullptr)
            content = EA::makeOwned<eacp::Graphics::View>();

        return *content;
    }

    EditorSize initialSize() const override { return HostFixtures::initialSize; }

    // Not resizable: the user may not drag it, yet it resizes itself.
    EditorConstraints constraints() const override { return {}; }

    void onAttached() override
    {
        eacp::Threads::callAsync(
            [this, token = alive]
            {
                if (*token && host() != nullptr)
                    requestOwnResize(resizedSize);
            });
    }

private:
    OwningPointer<eacp::Graphics::View> content;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace

OwningPointer<Editor> createInstrumentEditor()
{
    return EA::makeOwned<FixedSizeEditor>();
}

} // namespace HostFixtures
