#include "CLAPEvents.h"

#include <cstring>

namespace MakeASound::Hosting
{

namespace
{
bool fitsCLAPEvent(const clap_event_header& event) noexcept
{
    return event.size >= sizeof(clap_event_header)
           && event.size <= sizeof(CLAPEvent);
}
} // namespace

CLAPInputEvents::CLAPInputEvents()
{
    list.ctx = this;
    list.size = [](const clap_input_events* l)
    {
        return static_cast<uint32_t>(
            static_cast<const CLAPInputEvents*>(l->ctx)->size());
    };
    list.get = [](const clap_input_events* l, uint32_t index)
    {
        const auto& self = *static_cast<const CLAPInputEvents*>(l->ctx);
        return index < static_cast<uint32_t>(self.count)
                   ? self.at(static_cast<int>(index))
                   : nullptr;
    };
}

void CLAPInputEvents::reserve(int capacityToUse)
{
    auto size = static_cast<size_t>(capacityToUse > 0 ? capacityToUse : 0);
    storage.assign(size, CLAPEvent {});
    order.assign(size, 0);
    count = 0;
}

bool CLAPInputEvents::add(const clap_event_header& event) noexcept
{
    if (count >= capacity() || !fitsCLAPEvent(event))
        return false;

    auto& slot = storage[static_cast<size_t>(count)];
    slot = CLAPEvent {};
    std::memcpy(&slot, &event, event.size);

    order[static_cast<size_t>(count)] = count;
    ++count;
    return true;
}

const clap_event_header* CLAPInputEvents::at(int index) const noexcept
{
    if (index < 0 || index >= count)
        return nullptr;

    return &storage[static_cast<size_t>(order[static_cast<size_t>(index)])].header;
}

CLAPEvent* CLAPInputEvents::mutableAt(int index) noexcept
{
    if (index < 0 || index >= count)
        return nullptr;

    return &storage[static_cast<size_t>(order[static_cast<size_t>(index)])];
}

void CLAPInputEvents::sortByTime() noexcept
{
    auto timeOf = [this](int slot) noexcept
    { return storage[static_cast<size_t>(slot)].header.time; };

    for (auto i = 1; i < count; ++i)
    {
        auto moving = order[static_cast<size_t>(i)];
        auto j = i;

        // Strictly later only, so equal times keep their order.
        while (j > 0 && timeOf(order[static_cast<size_t>(j - 1)]) > timeOf(moving))
        {
            order[static_cast<size_t>(j)] = order[static_cast<size_t>(j - 1)];
            --j;
        }

        order[static_cast<size_t>(j)] = moving;
    }
}

CLAPOutputEvents::CLAPOutputEvents()
{
    list.ctx = this;
    list.try_push = [](const clap_output_events* l, const clap_event_header* event)
    { return static_cast<CLAPOutputEvents*>(l->ctx)->push(event); };
}

void CLAPOutputEvents::reserve(int capacityToUse)
{
    storage.assign(static_cast<size_t>(capacityToUse > 0 ? capacityToUse : 0),
                   CLAPEvent {});
    count = 0;
}

bool CLAPOutputEvents::push(const clap_event_header* event) noexcept
{
    if (event == nullptr || count >= static_cast<int>(storage.size())
        || !fitsCLAPEvent(*event))
        return false;

    auto& slot = storage[static_cast<size_t>(count)];
    slot = CLAPEvent {};
    std::memcpy(&slot, event, event->size);
    ++count;
    return true;
}

const clap_event_header* CLAPOutputEvents::at(int index) const noexcept
{
    if (index < 0 || index >= count)
        return nullptr;

    return &storage[static_cast<size_t>(index)].header;
}

} // namespace MakeASound::Hosting
