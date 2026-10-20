#pragma once

#include <cstdint>
#include <span>
#include <vector>

struct OpusEncoder;

// Wraps an Opus encoder configured for low-latency voice (VOIP mode).
// Accepts 20 ms PCM frames and produces compressed Opus packets.
class VoiceEncoder
{
public:
  VoiceEncoder();
  ~VoiceEncoder();

  VoiceEncoder(const VoiceEncoder&) = delete;
  VoiceEncoder& operator=(const VoiceEncoder&) = delete;

  // Initialise the encoder.  Must be called before Encode().
  bool Init();

  // Encode a single 20 ms PCM frame (960 float samples @ 48 kHz, mono).
  // Returns the number of bytes written to outBuffer, or 0 on failure.
  int Encode(std::span<const float> pcmFrame, std::vector<std::uint8_t>& outBuffer);

private:
  OpusEncoder* mEncoder{};
};
