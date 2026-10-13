// Compiled into each <Name>-AAX module by makeasound_add_plugin, never into the
// static target: the plugin's own TU defines describeModule(), and the bundle id
// arrives as MAKEASOUND_AAX_BUNDLE_ID and the bundle's name as
// MAKEASOUND_AAX_PACKAGE_NAME.
#include "Adapter.h"
#include "Describe.h"
#include "../Realtime/MessageThread.h"

// The SDK's ACF glue, with its shutdown renamed so ours can run first.
#define AAXShutdown AAXShutdown_SDK
#include <AAXLibrary/source/AAX_Init.cpp>
#undef AAXShutdown

std::string_view MakeASound::AAX::moduleBundleId() noexcept
{
    return MAKEASOUND_AAX_BUNDLE_ID;
}

AAX_Result GetEffectDescriptions(AAX_ICollection* collection)
{
    if (collection == nullptr)
        return AAX_ERROR_NULL_OBJECT;

    try
    {
        namespace Format = MakeASound::AAX;

        MakeASound::adoptHostMessageThread();
        return Format::describeEffects(*collection,
                                       Format::Adapter::module(),
                                       Format::moduleBundleId(),
                                       MAKEASOUND_AAX_PACKAGE_NAME);
    }
    catch (...)
    {
        return AAX_ERROR_UNKNOWN_EXCEPTION;
    }
}

// The host unloads the module after this, so nothing of eacp's may outlive it.
AAX_Result AAXShutdown(IACFUnknown* host)
{
    MakeASound::releaseHostMessageThread();
    return AAXShutdown_SDK(host);
}
