#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "../../common/VoicePacket.h"
#include "VoiceDecoder.h"

// Adaptive per-sender jitter buffer for Opus voice frames.
//
// Packets arrive over UDP with variable delay, out of order or not at all.
// The buffer stores *encoded* packets indexed by sequence number and is
// drained by the audio output clock, one 20 ms frame at a time:
//
//   * Playout only starts once enough audio is buffered to cover the
//     currently observed network jitter (adaptive target delay).
//   * A missing frame is reconstructed from the next packet's in-band FEC
//     data when available, otherwise Opus packet-loss concealment is used.
//   * If the buffered latency grows well beyond the target (clock drift or a
//     burst after a network stall) frames are dropped to catch up.
//   * When the sender stops talking (VAD gate closed) the buffer runs dry,
//     plays a short PLC fade-out and goes idle until the next talk spurt.
//
// All methods are called from the audio thread only and never allocate.
class JitterBuffer
{
public:
  // Ring capacity in frames (1.28 s). Must be a power of two.
  static constexpr std::size_t kSlots = 64;

  static constexpr int kMinTargetFrames = 2;   //  40 ms
  static constexpr int kMaxTargetFrames = 12;  // 240 ms

  JitterBuffer();

  bool Init();

  // Store an encoded packet. `arrivalUs` is the steady-clock arrival time in
  // microseconds, used for jitter estimation.
  void Insert(std::uint32_t seq, std::span<const std::uint8_t> opus, std::int64_t arrivalUs);

  // Produce the next 20 ms of PCM into `out` (kFrameSize samples).
  // Returns false (and leaves `out` untouched) while idle/buffering.
  bool PullFrame(std::span<float> out, std::int64_t nowUs);

  // Discard everything and return to the idle state.
  void Reset();

  bool IsIdle() const { return mState != State::Playing; }
  bool IsEmpty() const { return mCount == 0; }
  int  TargetFrames() const { return mTargetFrames; }

  // Statistics (for diagnostics).
  struct Stats
  {
    std::uint32_t played{0};
    std::uint32_t concealed{0};   // PLC frames
    std::uint32_t recovered{0};   // FEC frames
    std::uint32_t late{0};        // arrived after their playout time
    std::uint32_t dropped{0};     // discarded to reduce latency
    std::uint32_t underruns{0};   // ran dry (end of talk spurt or stall)
  };
  const Stats& GetStats() const { return mStats; }

private:
  enum class State { Idle, Buffering, Playing };

  struct Slot
  {
    bool          valid{false};
    std::uint32_t seq{0};
    std::uint16_t size{0};
    std::array<std::uint8_t, Voice::kMaxEncodedBytes> data{};
  };

  Slot&       SlotFor(std::uint32_t seq)       { return mSlots[seq & (kSlots - 1)]; }
  const Slot* Find(std::uint32_t seq) const;
  void        Consume(std::uint32_t seq);
  void        UpdateJitter(std::uint32_t seq, std::int64_t arrivalUs);
  int         BufferedFrames() const;  // nextSeq .. newest, inclusive

  VoiceDecoder mDecoder;
  std::array<Slot, kSlots> mSlots{};

  State         mState{State::Idle};
  std::size_t   mCount{0};          // number of valid slots
  std::uint32_t mNextSeq{0};        // next sequence number to play
  std::uint32_t mNewestSeq{0};      // highest sequence number stored
  std::int64_t  mBufferingSinceUs{0};
  int           mMissingRun{0};     // consecutive frames with no packet

  // Jitter estimation (relative to the expected 20 ms packet spacing).
  bool          mHaveRef{false};
  std::uint32_t mRefSeq{0};
  std::int64_t  mRefArrivalUs{0};
  float         mJitterPeakMs{0.0f};
  int           mTargetFrames{kMinTargetFrames};

  Stats mStats;
};
