#include "VoiceEncoder.h"

#include <print>
#include <opus.h>

#include "../../common/VoicePacket.h"

VoiceEncoder::VoiceEncoder() = default;

VoiceEncoder::~VoiceEncoder()
{
  if (mEncoder)
  {
    opus_encoder_destroy(mEncoder);
    mEncoder = nullptr;
  }
}

bool VoiceEncoder::Init()
{
  int err = 0;
  mEncoder = opus_encoder_create(Voice::kSampleRate, Voice::kChannels,
                                 OPUS_APPLICATION_VOIP, &err);
  if (err != OPUS_OK || mEncoder == nullptr)
  {
    std::println("[Voice] Failed to create Opus encoder: {}", opus_strerror(err));
    return false;
  }

  // Silence is already gated by our own VAD, so DTX is left off: every frame
  // we send carries real audio and the sequence numbers stay contiguous,
  // which the receiver's jitter buffer relies on to detect losses.
  opus_encoder_ctl(mEncoder, OPUS_SET_DTX(0));

  opus_encoder_ctl(mEncoder, OPUS_SET_BITRATE(32000));
  opus_encoder_ctl(mEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  opus_encoder_ctl(mEncoder, OPUS_SET_COMPLEXITY(8));

  // In-band forward error correction: each packet carries a low-bitrate copy
  // of the previous frame so the receiver can rebuild a single lost packet.
  opus_encoder_ctl(mEncoder, OPUS_SET_INBAND_FEC(1));
  opus_encoder_ctl(mEncoder, OPUS_SET_PACKET_LOSS_PERC(10));

  std::println("[Voice] Opus encoder initialised (VOIP, 32 kbps, in-band FEC).");
  return true;
}

int VoiceEncoder::Encode(std::span<const float> pcmFrame,
                         std::vector<std::uint8_t>& outBuffer)
{
  if (!mEncoder)
    return 0;

  outBuffer.resize(static_cast<std::size_t>(Voice::kMaxEncodedBytes));

  const int encodedBytes = opus_encode_float(
    mEncoder,
    pcmFrame.data(),
    Voice::kFrameSize,
    outBuffer.data(),
    static_cast<opus_int32>(outBuffer.size()));

  if (encodedBytes < 0)
  {
    std::println("[Voice] Opus encode error: {}", opus_strerror(encodedBytes));
    outBuffer.clear();
    return 0;
  }

  outBuffer.resize(static_cast<std::size_t>(encodedBytes));
  return encodedBytes;
}
