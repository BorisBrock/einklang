#pragma once

#include <cstdint>
#include <span>

struct OpusDecoder;

// Wraps an Opus decoder.  One instance should be created per remote speaker
// because each decoder maintains its own internal state for packet loss
// concealment (PLC).
//
// All decode functions write into caller-provided buffers and never allocate,
// so they are safe to call from the real-time audio thread.
class VoiceDecoder
{
public:
  VoiceDecoder();
  ~VoiceDecoder();

  VoiceDecoder(const VoiceDecoder&) = delete;
  VoiceDecoder& operator=(const VoiceDecoder&) = delete;

  // Initialise the decoder.  Must be called before any Decode*() call.
  bool Init();

  // Decode a compressed Opus frame into `out` (kFrameSize samples).
  // Returns the number of decoded samples, or 0 on failure.
  int Decode(std::span<const std::uint8_t> opusData, std::span<float> out);

  // Recover the frame *preceding* `nextOpusData` from its in-band FEC data.
  // Falls back to plain PLC inside Opus if the packet carries no FEC.
  int DecodeFec(std::span<const std::uint8_t> nextOpusData, std::span<float> out);

  // Generate a PLC frame (conceals a lost packet).  Returns sample count.
  int DecodePlc(std::span<float> out);

  // Reset internal state (e.g. when a new talk spurt starts after a long gap).
  void Reset();

private:
  OpusDecoder* mDecoder{};
};
