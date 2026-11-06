#include "StandaloneApp.h"
#include <eacp/Core/App/App.h>

int main()
{
    return eacp::Apps::run<MakeASound::Standalone::StandaloneApp>();
}
