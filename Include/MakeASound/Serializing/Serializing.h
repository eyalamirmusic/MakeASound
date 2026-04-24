#pragma once

#include <Miro/Miro.h>
#include "../DeviceInfo.h"

MIRO_REFLECT_EXTERNAL(MakeASound::DeviceInfo,
                      id,
                      name,
                      outputChannels,
                      inputChannels,
                      duplexChannels,
                      isDefaultOutput,
                      isDefaultInput,
                      sampleRates,
                      currentSampleRate,
                      preferredSampleRate,
                      nativeFormats)

MIRO_REFLECT_EXTERNAL(MakeASound::StreamParameters, device, nChannels, firstChannel)

MIRO_REFLECT_EXTERNAL(MakeASound::Flags,
                      nonInterleaved,
                      minimizeLatency,
                      hogDevice,
                      scheduleRealTime,
                      alsaUseDefault,
                      jackDontConnect)

MIRO_REFLECT_EXTERNAL(MakeASound::StreamOptions,
                      flags,
                      numberOfBuffers,
                      streamName,
                      priority)

MIRO_REFLECT_EXTERNAL(MakeASound::StreamConfig,
                      input,
                      output,
                      format,
                      sampleRate,
                      maxBlockSize,
                      options)
