#pragma once

/** @file
    The SDK-free plugin core. A plugin derives from Plugin (or StatePlugin, which
    saves and loads through a reflected State), declares its parameters as
    Parameter members of a ParameterGroup, and implements describeModule() once
    per module. The VST3, AU, AUv3, AAX and standalone adapters each include this
    header, add their SDK, and drive the plugin through PluginWrapper.

    - A plugin source file includes this header and nothing else from the
      framework.
    - process() runs on the audio thread; parameters are relaxed atomics read
      there. Editors, state loads and host notifications are message thread.
    - A parameter's host id and state key come from its id path: pin `id` or
      `hostId` before renaming or regrouping a shipped parameter.
    - Hand objects to the audio thread through RealtimeSwap, never a lock.
*/

#include "../MakeASound.h"
#include "Core/Description.h"
#include "Core/IdentityPolicy.h"
#include "Core/Identity.h"
#include "Parameters/ParameterIdentity.h"
#include "Parameters/Parameter.h"
#include "Parameters/FloatRange.h"
#include "Parameters/FloatParam.h"
#include "Parameters/IntParam.h"
#include "Parameters/ChoiceParam.h"
#include "Parameters/BoolParam.h"
#include "Parameters/CommonParams.h"
#include "Parameters/ParameterGroup.h"
#include "Parameters/ParameterList.h"
#include "Parameters/IdPath.h"
#include "State/StateContext.h"
#include "State/State.h"
#include "Core/HostEditListener.h"
#include "Core/TrackInfo.h"
#include "Core/HostProcess.h"
#include "Core/Editor.h"
#include "Core/Plugin.h"
#include "Core/StatePlugin.h"
#include "Host/PluginWrapper.h"
#include "Realtime/RealtimeSwap.h"
#include "Compat/JuceTree.h"
#include "Compat/JuceState.h"
#include "Compat/ApvtsXml.h"
#include "Compat/JuceIdentity.h"
