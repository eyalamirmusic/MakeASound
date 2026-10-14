#include "State.h"
#include "../Parameters/IdPath.h"

#include <Miro/JSON/Json.h>

#include <cstring>

namespace MakeASound::AAX
{

namespace
{
constexpr char chunkName[] = "MakeASound State";

Miro::Json::Value paramsOf(std::string_view document, std::string_view bypassId)
{
    auto parsed = Miro::Json::getParsedValue(document);

    if (!parsed.isObject())
        return {};

    auto* params = Miro::Json::find(parsed.asObject(), "params");

    if (params == nullptr || !params->isObject())
        return {};

    auto result = *params;
    auto* object = &result.asObject();
    auto keys = splitIdPath(bypassId);

    for (auto i = 0; i < keys.size(); ++i)
    {
        const auto& key = keys[i];

        if (i == keys.size() - 1)
        {
            if (auto found = object->find(key); found != object->end())
                object->erase(found);

            break;
        }

        auto* child = Miro::Json::find(*object, key);

        if (child == nullptr || !child->isObject())
            break;

        object = &child->asObject();
    }

    return result;
}
} // namespace

bool writeChunk(AAX_SPlugInChunk& chunk, std::string_view document) noexcept
{
    if (chunk.fSize < 0 || static_cast<size_t>(chunk.fSize) < document.size())
        return false;

    chunk.fVersion = 1;
    std::memset(chunk.fName, 0, sizeof(chunk.fName));
    std::memcpy(chunk.fName, chunkName, sizeof(chunkName));
    std::memcpy(chunk.fData, document.data(), document.size());
    chunk.fSize = static_cast<int32_t>(document.size());
    return true;
}

std::string_view readChunk(const AAX_SPlugInChunk& chunk) noexcept
{
    if (chunk.fSize <= 0)
        return {};

    return {chunk.fData, static_cast<size_t>(chunk.fSize)};
}

bool sameParameters(std::string_view document,
                    std::string_view other,
                    std::string_view bypassId)
{
    return paramsOf(document, bypassId) == paramsOf(other, bypassId);
}

} // namespace MakeASound::AAX
