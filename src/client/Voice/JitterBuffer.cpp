#include "JitterBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr std::int64_t kFrameUs = Voice::kFrameMs * 1000;

// Latency control: drop frames once more than this many frames beyond the
// target delay are queued.
constexpr int kExcessFrames = 3;

// Maximum run of concealed frames inserted while waiting for a late packet.
// Also forms the fade-out tail after a talk spurt ends (Opus PLC decays to
// silence on its own), and bridges short network stalls.
constexpr int kMaxWaitFrames = 5;

// Per-packet decay of the jitter peak (half-life ≈ 230 packets ≈ 4.6 s).
constexpr float kJitterDecay = 0.997f;

// Signed distance a - b between two wrapping 32-bit sequence numbers.
std::int32_t SeqDiff(std::uint32_t a, std::uint32_t b)
{
  return static_cast<std::int32_t>(a - b);
}
} // namespace

JitterBuffer::JitterBuffer() = default;

bool JitterBuffer::Init()
{
  return mDecoder.Init();
}

void JitterBuffer::Reset()
{
  for (auto& slot : mSlots)
    slot.valid = false;
  mCount      = 0;
  mState      = State::Idle;
  mMissingRun = 0;
  mHaveRef    = false;
}

const JitterBuffer::Slot* JitterBuffer::Find(std::uint32_t seq) const
{
  const Slot& slot = mSlots[seq & (kSlots - 1)];
  return (slot.valid && slot.seq == seq) ? &slot : nullptr;
}

void JitterBuffer::Consume(std::uint32_t seq)
{
  Slot& slot = SlotFor(seq);
  if (slot.valid && slot.seq == seq)
  {
    slot.valid = false;
    --mCount;
  }
}

int JitterBuffer::BufferedFrames() const
{
  if (mCount == 0)
    return 0;
  return SeqDiff(mNewestSeq, mNextSeq) + 1;
}

void JitterBuffer::Insert(std::uint32_t seq, std::span<const std::uint8_t> opus,
                          std::int64_t arrivalUs)
{
  if (opus.empty() || opus.size() > Voice::kMaxEncodedBytes)
    return;

  if (mState == State::Playing)
  {
    const std::int32_t diff = SeqDiff(seq, mNextSeq);
    if (diff < 0 && diff > -static_cast<std::int32_t>(kSlots))
    {
      ++mStats.late; // its playout time has already passed
      return;
    }
    if (diff < 0 || diff >= static_cast<std::int32_t>(kSlots))
      Reset(); // sender restarted its sequence counter
  }
  else if (mState == State::Buffering)
  {
    const std::int32_t diff = SeqDiff(seq, mNextSeq);
    const std::int32_t span = BufferedFrames();
    if (diff >= static_cast<std::int32_t>(kSlots) ||
        (diff < 0 && span - diff > static_cast<std::int32_t>(kSlots)))
      Reset();
    else if (diff < 0)
      mNextSeq = seq; // reordered packet from the start of the talk spurt
  }

  if (mState == State::Idle)
  {
    // First packet of a new talk spurt.
    mState            = State::Buffering;
    mBufferingSinceUs = arrivalUs;
    mNextSeq          = seq;
    mNewestSeq        = seq;
    mHaveRef          = false;
  }

  Slot& slot = SlotFor(seq);
  if (slot.valid)
  {
    if (slot.seq == seq)
      return; // duplicate
    --mCount; // stale entry, should not happen within the window
  }

  slot.valid = true;
  slot.seq   = seq;
  slot.size  = static_cast<std::uint16_t>(opus.size());
  std::memcpy(slot.data.data(), opus.data(), opus.size());
  ++mCount;

  if (SeqDiff(seq, mNewestSeq) > 0)
    mNewestSeq = seq;

  UpdateJitter(seq, arrivalUs);
}

void JitterBuffer::UpdateJitter(std::uint32_t seq, std::int64_t arrivalUs)
{
  if (!mHaveRef)
  {
    mHaveRef      = true;
    mRefSeq       = seq;
    mRefArrivalUs = arrivalUs;
    return;
  }

  // How much later than the reference timeline did this packet arrive?
  const std::int64_t expectedUs = mRefArrivalUs + SeqDiff(seq, mRefSeq) * kFrameUs;
  std::int64_t lateUs = arrivalUs - expectedUs;
  if (lateUs < 0)
  {
    // Faster than the reference packet: it becomes the new baseline.
    mRefSeq       = seq;
    mRefArrivalUs = arrivalUs;
    lateUs        = 0;
  }

  const float lateMs = static_cast<float>(lateUs) / 1000.0f;
  mJitterPeakMs = std::max(lateMs, mJitterPeakMs * kJitterDecay);

  const int frames = 1 + static_cast<int>(std::ceil(mJitterPeakMs / Voice::kFrameMs));
  mTargetFrames = std::clamp(frames, kMinTargetFrames, kMaxTargetFrames);
}

bool JitterBuffer::PullFrame(std::span<float> out, std::int64_t nowUs)
{
  if (mState == State::Idle)
    return false;

  if (mState == State::Buffering)
  {
    const bool enoughBuffered = BufferedFrames() >= mTargetFrames;
    // Short utterances may never fill the buffer; start anyway once the
    // first packet has waited for the target delay.
    const bool waitedLongEnough = nowUs - mBufferingSinceUs >= mTargetFrames * kFrameUs;
    if (!enoughBuffered && !waitedLongEnough)
      return false;

    mState      = State::Playing;
    mMissingRun = 0;
  }

  // Latency control: catch up if far more audio is queued than needed.
  while (BufferedFrames() > mTargetFrames + kExcessFrames)
  {
    if (const Slot* slot = Find(mNextSeq))
    {
      // Decode and discard to keep the decoder state continuous.
      mDecoder.Decode({slot->data.data(), slot->size}, out);
      Consume(mNextSeq);
    }
    ++mNextSeq;
    ++mStats.dropped;
  }

  if (const Slot* slot = Find(mNextSeq))
  {
    const int n = mDecoder.Decode({slot->data.data(), slot->size}, out);
    Consume(mNextSeq);
    ++mNextSeq;
    mMissingRun = 0;
    ++mStats.played;
    if (n <= 0)
      std::fill(out.begin(), out.end(), 0.0f);
    return true;
  }

  // The frame we need is missing.  If less audio is queued than the jitter
  // target asks for, it is most likely just late: conceal *without*
  // advancing, which grows the playout delay by one frame and gives the
  // packet time to arrive.
  if (BufferedFrames() < mTargetFrames && mMissingRun < kMaxWaitFrames)
  {
    if (mDecoder.DecodePlc(out) <= 0)
      std::fill(out.begin(), out.end(), 0.0f);
    ++mMissingRun;
    ++mStats.concealed;
    return true;
  }

  if (mCount > 0)
  {
    // Newer packets exist, so this one was lost or is very late.
    // Use the following packet's FEC data if we already have it.
    int n = 0;
    if (const Slot* next = Find(mNextSeq + 1))
    {
      n = mDecoder.DecodeFec({next->data.data(), next->size}, out);
      ++mStats.recovered;
    }
    else
    {
      n = mDecoder.DecodePlc(out);
      ++mStats.concealed;
    }
    if (n <= 0)
      std::fill(out.begin(), out.end(), 0.0f);
    ++mNextSeq;
    ++mMissingRun;
    return true;
  }

  // Buffer ran dry: the talk spurt ended (or the network stalled for longer
  // than we are willing to conceal).  Re-buffer when packets arrive again.
  ++mStats.underruns;
  mState      = State::Idle;
  mMissingRun = 0;
  return false;
}
