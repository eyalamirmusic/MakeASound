#include "FormatManager.h"
#include "Scan/Scanner.h"

#include <filesystem>

namespace MakeASound::Hosting
{

bool HostFormat::targetExists(std::string_view target) const
{
    auto ec = std::error_code();

    return !target.empty()
           && std::filesystem::exists(std::filesystem::path(std::string(target)), ec)
           && !ec;
}

ModuleStamp HostFormat::stamp(std::string_view target) const
{
    return moduleStamp(target);
}

LoadResult HostFormat::load(const PluginDescription& description,
                            const HostInfo&) const
{
    auto result = LoadResult();
    result.error = "this format cannot load " + description.path;
    return result;
}

FormatManager::FormatManager() = default;
FormatManager::~FormatManager() = default;

void FormatManager::add(OwningPointer<HostFormat> formatToAdd)
{
    if (formatToAdd == nullptr)
        return;

    for (auto& existing: formats)
    {
        if (existing->format() == formatToAdd->format())
        {
            existing = std::move(formatToAdd);
            return;
        }
    }

    formats.add(std::move(formatToAdd));
}

int FormatManager::size() const noexcept
{
    return formats.size();
}

const HostFormat& FormatManager::operator[](int index) const noexcept
{
    return *formats[index];
}

const HostFormat* FormatManager::find(PluginFormat format) const noexcept
{
    for (const auto& candidate: formats)
        if (candidate->format() == format)
            return candidate.get();

    return nullptr;
}

const HostFormat* FormatManager::findForTarget(std::string_view target) const
{
    if (target.empty())
        return nullptr;

    for (const auto& candidate: formats)
        if (candidate->ownsTarget(target))
            return candidate.get();

    return nullptr;
}

ModuleStamp FormatManager::stamp(const ModuleScan& entry) const
{
    if (const auto* format = find(entry.format))
        return format->stamp(entry.path);

    return moduleStamp(entry.path);
}

PluginCache::StampFunction FormatManager::stampFunction() const
{
    return [this](const ModuleScan& entry) { return stamp(entry); };
}

ModuleStamp FormatManager::stampTarget(std::string_view target) const
{
    if (const auto* format = findForTarget(target))
        return format->stamp(target);

    return moduleStamp(target);
}

PluginFormat FormatManager::formatOfTarget(std::string_view target) const
{
    if (const auto* format = findForTarget(target))
        return format->format();

    return PluginFormat::VST3;
}

LoadResult FormatManager::load(const PluginDescription& description,
                               const HostInfo& host) const
{
    if (const auto* format = find(description.format))
        return format->load(description, host);

    auto result = LoadResult();
    result.error = "no format in this build loads " + description.name;
    return result;
}

TargetScan FormatManager::scanTarget(std::string_view target) const
{
    auto outcome = TargetScan();
    const auto* format = findForTarget(target);

    if (format == nullptr)
    {
        outcome.request = TargetRequest::unknownKind;
        outcome.message =
            "no plugin format in this build claims '" + std::string(target) + "'";

        return outcome;
    }

    if (!format->targetExists(target))
    {
        outcome.request = TargetRequest::noSuchTarget;
        outcome.message = "nothing is installed as " + std::string(target);

        return outcome;
    }

    // From here on everything is an answer, including a plugin that refuses to
    // load. The target and the claiming format are what the cache keys and
    // stamps on, so they win over whatever the scan echoed.
    outcome.scan = format->scan(target);
    outcome.scan.path = std::string(target);
    outcome.scan.format = format->format();

    return outcome;
}

} // namespace MakeASound::Hosting
