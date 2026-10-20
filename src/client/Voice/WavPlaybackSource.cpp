// NOTE: Do NOT define MINIAUDIO_IMPLEMENTATION here — it is already defined
// in VoiceCapture.cpp.  miniaudio is header-only and the implementation must
// appear exactly once in the entire program.
#include <miniaudio.h>

#include "WavPlaybackSource.h"

#include <chrono>
#include <cmath>
#include <print>
#include <vector>

#include "../../common/VoicePacket.h"

WavPlaybackSource::WavPlaybackSource() = default;

WavPlaybackSource::~WavPlaybackSource()
{
  Stop();
}

bool WavPlaybackSource::Init(const std::string& filePath)
{
  // Try to open the file immediately so we can report a clear error during
  // VoicePipeline::Init() rather than failing silently at Start().
  ma_decoder_config cfg = ma_decoder_config_init(
      ma_format_f32, Voice::kChannels, Voice::kSampleRate);
  ma_decoder probe;
  if (ma_decoder_init_file(filePath.c_str(), &cfg, &probe) != MA_SUCCESS)
  {
    std::println("[WavPlayback] Cannot open file: {}", filePath);
    return false;
  }
  ma_decoder_uninit(&probe);

  mFilePath = filePath;
  std::println("[WavPlayback] Validated playback file: {}", mFilePath);
  return true;
}

bool WavPlaybackSource::Start()
{
  if (mRunning.load(std::memory_order_relaxed))
    return true;

  mRunning.store(true, std::memory_order_relaxed);
  mThread = std::thread([this]() { workerThreadFunc(); });
  return true;
}

void WavPlaybackSource::Stop()
{
  mRunning.store(false, std::memory_order_relaxed);
  if (mThread.joinable())
    mThread.join();
}

bool WavPlaybackSource::IsRunning() const
{
  return mRunning.load(std::memory_order_relaxed);
}

void WavPlaybackSource::SetFrameCallback(FrameCallback cb)
{
  mCallback = std::move(cb);
}

void WavPlaybackSource::SetMuted(bool muted)
{
  mMuted.store(muted, std::memory_order_relaxed);
}

bool WavPlaybackSource::IsMuted() const
{
  return mMuted.load(std::memory_order_relaxed);
}

void WavPlaybackSource::SetInputGain(float gain)
{
  mGain.store(gain, std::memory_order_relaxed);
}

float WavPlaybackSource::InputGain() const
{
  return mGain.load(std::memory_order_relaxed);
}

float WavPlaybackSource::GetLastRms() const
{
  return mLastRms.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Worker thread — runs at real-time pace, ~20 ms per frame
// ---------------------------------------------------------------------------
void WavPlaybackSource::workerThreadFunc()
{
  ma_decoder_config cfg = ma_decoder_config_init(
      ma_format_f32, Voice::kChannels, Voice::kSampleRate);
  ma_decoder decoder;
  if (ma_decoder_init_file(mFilePath.c_str(), &cfg, &decoder) != MA_SUCCESS)
  {
    std::println("[WavPlayback] Failed to open '{}' in worker thread.", mFilePath);
    mRunning.store(false, std::memory_order_relaxed);
    return;
  }

  std::println("[WavPlayback] Streaming '{}' ({} Hz, mono, looping).", mFilePath, Voice::kSampleRate);

  constexpr auto kFrameSize = static_cast<std::size_t>(Voice::kFrameSize);
  std::vector<float> frame(kFrameSize);

  // Target exactly one 20 ms frame per iteration.
  using Clock = std::chrono::steady_clock;
  const auto frameDuration = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(static_cast<double>(kFrameSize) / Voice::kSampleRate));

  auto nextFrameTime = Clock::now() + frameDuration;

  while (mRunning.load(std::memory_order_relaxed))
  {
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, frame.data(), kFrameSize, &framesRead);

    if (framesRead < kFrameSize)
    {
      // Pad the remainder with silence then seek back to restart.
      std::fill(frame.begin() + static_cast<std::ptrdiff_t>(framesRead),
                frame.end(), 0.0f);
      ma_decoder_seek_to_pcm_frame(&decoder, 0);
      std::println("[WavPlayback] Looping '{}'.", mFilePath);
    }

    // Apply input gain and compute RMS.
    const float gain = mGain.load(std::memory_order_relaxed);
    float sumSq = 0.f;
    for (float& s : frame)
    {
      if (gain != 1.0f) s *= gain;
      sumSq += s * s;
    }
    mLastRms.store(std::sqrt(sumSq / static_cast<float>(kFrameSize)),
                   std::memory_order_relaxed);

    // Fire the callback only when not muted.
    if (!mMuted.load(std::memory_order_relaxed) && mCallback)
      mCallback(std::span<const float>(frame.data(), kFrameSize));

    // Pace delivery to keep the encoder ring at a steady rate.
    std::this_thread::sleep_until(nextFrameTime);
    nextFrameTime += frameDuration;
  }

  ma_decoder_uninit(&decoder);
  std::println("[WavPlayback] Worker stopped.");
}
