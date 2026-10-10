#pragma once

// The SDK-free plugin core. A plugin TU includes this and nothing else from the
// framework; the per-format adapters include it too and add their SDK.

#include "../MakeASound.h"
#include "Description.h"
#include "Parameter.h"
#include "FloatParam.h"
#include "ChoiceParam.h"
#include "BoolParam.h"
#include "CommonParams.h"
#include "ParameterGroup.h"
#include "ParameterList.h"
#include "StateContext.h"
#include "State.h"
#include "HostEditListener.h"
#include "Editor.h"
#include "Plugin.h"
#include "StatePlugin.h"
#include "PluginWrapper.h"
#include "Realtime/RealtimeSwap.h"
