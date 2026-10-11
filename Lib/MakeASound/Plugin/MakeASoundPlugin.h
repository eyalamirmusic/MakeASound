#pragma once

// The SDK-free plugin core. A plugin TU includes this and nothing else from the
// framework; the per-format adapters include it too and add their SDK.

#include "../MakeASound.h"
#include "Core/Description.h"
#include "Parameters/Parameter.h"
#include "Parameters/FloatParam.h"
#include "Parameters/ChoiceParam.h"
#include "Parameters/BoolParam.h"
#include "Parameters/CommonParams.h"
#include "Parameters/ParameterGroup.h"
#include "Parameters/ParameterList.h"
#include "State/StateContext.h"
#include "State/State.h"
#include "Core/HostEditListener.h"
#include "Core/Editor.h"
#include "Core/Plugin.h"
#include "Core/StatePlugin.h"
#include "Host/PluginWrapper.h"
#include "Realtime/RealtimeSwap.h"
