#pragma once

// The SDK headers every adapter TU takes first. Nothing in MakeASoundPlugin ever
// includes them, so no Avid type reaches the plugin core.
#include <AAX.h>
#include <AAX_Enums.h>
#include <AAX_Errors.h>
#include <AAX_Properties.h>
#include <AAX_CEffectParameters.h>
#include <AAX_ICollection.h>
#include <AAX_IComponentDescriptor.h>
#include <AAX_IEffectDescriptor.h>
#include <AAX_IPropertyMap.h>
#include <AAX_IController.h>
#include <AAX_IMIDINode.h>
#include <AAX_ITransport.h>
#include <AAX_IEffectGUI.h>
