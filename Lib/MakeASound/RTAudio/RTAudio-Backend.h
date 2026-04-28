#pragma once

#include <RtAudio.h>
#include "../Common/Common.h"
#include "../DeviceInfo/DeviceInfo.h"

namespace MakeASound::RTAudio
{

template <typename BitType>
bool bitCompare(BitType bits, BitType bit)
{
    return bits & bit;
}

template <typename A, typename T, typename Func>
OwningPointer<A> optionalToPointer(const std::optional<T>& val, Func func)
{
    if (val.has_value())
        return EA::makeOwned<A>(func(val.value()));

    return {};
}

RtAudioFormat getFormat(Format format);
void addFormat(Formats& formats, RtAudioFormat bits, Format format);
Formats getFormats(RtAudioFormat formats);
DeviceInfo getInfo(const RtAudio::DeviceInfo& info);
Error getError(RtAudioErrorType error);
RtAudio::StreamParameters getStreamParams(const StreamParameters& params);
RtAudioStreamFlags getFlags(Flags flags);
RtAudio::StreamOptions getOptions(const StreamOptions& options);
AudioCallbackStatus getStatus(RtAudioStreamStatus status);
AudioCallbackInfo getCallbackInfo(void* outputBuffer,
                                  void* inputBuffer,
                                  unsigned int numSamples,
                                  double streamTime,
                                  RtAudioStreamStatus status,
                                  unsigned int sampleRate,
                                  unsigned int latency,
                                  const StreamConfig& config);

} // namespace MakeASound::RTAudio
