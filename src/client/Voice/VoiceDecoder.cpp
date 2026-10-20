#include "VoiceDecoder.h"

#include <print>
#include <opus.h>

#include "../../common/VoicePacket.h"

VoiceDecoder::VoiceDecoder() = default;

VoiceDecoder::~VoiceDecoder()
{
  if (mDecoder)
  {
    opus_decoder_destroy(mDecoder);
    mDecoder = nullptr;
  }
}

bool VoiceDecoder::Init()
{
  int err = 0;
  mDecoder = opus_decoder_create(Voice::kSampleRate, Voice::kChannels, &err);

  if (err != OPUS_OK || mDecoder == nullptr)
  {
    std::println("[Voice] Failed to create Opus decoder: {}", opus_strerror(err));
    return false;
  }

  return true;
}

int VoiceDecoder::Decode(std::span<const std::uint8_t> opusData, std::span<float> out)
{
  if (!mDecoder || out.size() < static_cast<std::size_t>(Voice::kFrameSize))
    return 0;

  const int decodedSamples = opus_decode_float(
    mDecoder,
    opusData.data(),
    static_cast<opus_int32>(opusData.size()),
    out.data(),
    Voice::kFrameSize,
    0);

  return decodedSamples < 0 ? 0 : decodedSamples;
}

int VoiceDecoder::DecodeFec(std::span<const std::uint8_t> nextOpusData, std::span<float> out)
{
  if (!mDecoder || out.size() < static_cast<std::size_t>(Voice::kFrameSize))
    return 0;

  // For FEC the requested frame size must match the duration of the lost
  // frame exactly (one 20 ms frame).
  const int decodedSamples = opus_decode_float(
    mDecoder,
    nextOpusData.data(),
    static_cast<opus_int32>(nextOpusData.size()),
    out.data(),
    Voice::kFrameSize,
    1 /* decode_fec */);

  return decodedSamples < 0 ? 0 : decodedSamples;
}

int VoiceDecoder::DecodePlc(std::span<float> out)
{
  if (!mDecoder || out.size() < static_cast<std::size_t>(Voice::kFrameSize))
    return 0;

  const int decodedSamples = opus_decode_float(
    mDecoder,
    nullptr, // null pointer triggers PLC
    0,
    out.data(),
    Voice::kFrameSize,
    0);

  return decodedSamples < 0 ? 0 : decodedSamples;
}

void VoiceDecoder::Reset()
{
  if (mDecoder)
    opus_decoder_ctl(mDecoder, OPUS_RESET_STATE);
}
