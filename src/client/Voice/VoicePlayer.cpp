// NOTE: Do NOT define MINIAUDIO_IMPLEMENTATION here — it is already defined
// in VoiceCapture.cpp.  miniaudio is header-only and the implementation must
// appear exactly once in the entire program.
#include <miniaudio.h>

#include "VoicePlayer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <print>

#include "../../common/SpscRingBuffer.h"
#include "../../common/VoicePacket.h"
#include "JitterBuffer.h"

namespace
{
// Maximum number of simultaneous remote speakers that can be mixed.
constexpr std::size_t kMaxSenders = 16;

// A sender slot is recycled after this long without any packets.
constexpr std::int64_t kSenderTimeoutUs = 10'000'000;

std::int64_t NowUs()
{
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

struct IncomingPacket
{
  std::uint32_t senderId{0};
  std::uint32_t seq{0};
  std::int64_t  arrivalUs{0};
  std::uint16_t size{0};
  std::array<std::uint8_t, Voice::kMaxEncodedBytes> data{};
};

struct SenderSlot
{
  bool          inUse{false};
  std::uint32_t senderId{0};
  std::int64_t  lastPacketUs{0};
  JitterBuffer  jitter;

  // Decoded frame currently being played out (callbacks rarely align with
  // 20 ms frame boundaries).
  std::array<float, Voice::kFrameSize> pcm{};
  std::size_t pcmPos{Voice::kFrameSize}; // == kFrameSize means "empty"
};
} // namespace

struct VoicePlayer::Impl
{
  ma_context context{};
  bool       contextInitialised{false};
  ma_device  device{};
  bool       deviceInitialised{false};
  std::atomic<bool> playing{false}; // read by the network thread
  bool       decodersReady{false};

  // Network thread → audio thread.
  SpscRingBuffer<IncomingPacket, 256> inbox;
  IncomingPacket scratch; // pop target, avoids a 1.3 kB stack copy per packet
  std::atomic<int> inboxDrops{0};

  std::array<SenderSlot, kMaxSenders> senders;

  std::atomic<float> volume{1.0f};
  OutputTap          tap;

  SenderSlot* SlotFor(std::uint32_t senderId, std::int64_t nowUs)
  {
    SenderSlot* freeSlot = nullptr;
    for (auto& s : senders)
    {
      if (s.inUse && s.senderId == senderId)
        return &s;
      if (!freeSlot)
      {
        const bool stale = s.inUse && s.jitter.IsIdle() &&
                           nowUs - s.lastPacketUs > kSenderTimeoutUs;
        if (!s.inUse || stale)
          freeSlot = &s;
      }
    }
    if (freeSlot)
    {
      freeSlot->inUse    = true;
      freeSlot->senderId = senderId;
      freeSlot->pcmPos   = Voice::kFrameSize;
      freeSlot->jitter.Reset();
    }
    return freeSlot;
  }

  void ResetSenders()
  {
    for (auto& s : senders)
    {
      s.inUse  = false;
      s.pcmPos = Voice::kFrameSize;
      s.jitter.Reset();
    }
  }
};

// ---------------------------------------------------------------------------
// miniaudio playback callback (real-time audio thread)
// ---------------------------------------------------------------------------
static void playbackCallback(ma_device* pDevice, void* pOutput,
                             const void* /*pInput*/, ma_uint32 frameCount)
{
  auto* impl   = static_cast<VoicePlayer::Impl*>(pDevice->pUserData);
  auto* output = static_cast<float*>(pOutput);
  const std::size_t requested    = frameCount;
  const std::size_t channelCount = pDevice->playback.channels;
  const std::int64_t nowUs       = NowUs();

  // 1. Move newly arrived packets into their sender's jitter buffer.
  while (impl->inbox.pop(impl->scratch))
  {
    const auto& pkt = impl->scratch;
    if (SenderSlot* slot = impl->SlotFor(pkt.senderId, nowUs))
    {
      slot->lastPacketUs = pkt.arrivalUs;
      slot->jitter.Insert(pkt.seq, {pkt.data.data(), pkt.size}, pkt.arrivalUs);
    }
  }

  // 2. Mix all active senders.  Output is mono-mixed first, then fanned out
  //    to the device channels in place (back to front so nothing is overwritten).
  std::memset(output, 0, requested * channelCount * sizeof(float));

  for (auto& s : impl->senders)
  {
    if (!s.inUse)
      continue;

    std::size_t written = 0;
    while (written < requested)
    {
      if (s.pcmPos >= Voice::kFrameSize)
      {
        if (!s.jitter.PullFrame(s.pcm, nowUs))
          break; // idle or still buffering: rest stays silent
        s.pcmPos = 0;
      }
      const std::size_t n = std::min(requested - written, Voice::kFrameSize - s.pcmPos);
      for (std::size_t i = 0; i < n; ++i)
        output[written + i] += s.pcm[s.pcmPos + i];
      s.pcmPos += n;
      written  += n;
    }
  }

  // 3. Volume and hard clip to prevent wrap-around when speakers overlap.
  const float vol = impl->volume.load(std::memory_order_relaxed);
  for (std::size_t i = 0; i < requested; ++i)
    output[i] = std::clamp(output[i] * vol, -1.0f, 1.0f);

  if (impl->tap)
    impl->tap(std::span<const float>(output, requested));

  if (channelCount > 1)
  {
    for (std::size_t i = requested; i-- > 0;)
      for (std::size_t ch = 0; ch < channelCount; ++ch)
        output[i * channelCount + ch] = output[i];
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
VoicePlayer::VoicePlayer()
  : mImpl(std::make_unique<Impl>())
{
}

VoicePlayer::~VoicePlayer()
{
  Stop();
  if (mImpl->contextInitialised)
    ma_context_uninit(&mImpl->context);
}

bool VoicePlayer::Start(bool headless)
{
  if (mImpl->playing)
    return true;

  // Opus decoders are created once, up front, so the audio thread never
  // allocates.
  if (!mImpl->decodersReady)
  {
    for (auto& s : mImpl->senders)
      if (!s.jitter.Init())
        return false;
    mImpl->decodersReady = true;
  }

  // Discard anything queued while we were stopped.  The device is not
  // running, so this thread may act as the consumer.
  while (mImpl->inbox.pop(mImpl->scratch)) {}
  mImpl->ResetSenders();

  // The device is created on every Start() call so that the WASAPI COM call
  // always happens inside the WebView2 message loop (see VoiceConnector).
  ma_context* context = nullptr;
  if (headless)
  {
    if (!mImpl->contextInitialised)
    {
      const ma_backend backends[] = {ma_backend_null};
      if (ma_context_init(backends, 1, nullptr, &mImpl->context) != MA_SUCCESS)
      {
        std::println("[Voice] Failed to initialise null audio backend.");
        return false;
      }
      mImpl->contextInitialised = true;
    }
    context = &mImpl->context;
  }

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format             = ma_format_f32;
  config.playback.channels           = Voice::kChannels;
  config.sampleRate                  = Voice::kSampleRate;
  config.periodSizeInMilliseconds    = 10;
  config.performanceProfile          = ma_performance_profile_low_latency;
  config.noPreSilencedOutputBuffer   = MA_TRUE; // the callback always writes every sample
  config.dataCallback                = playbackCallback;
  config.pUserData                   = mImpl.get();

  if (ma_device_init(context, &config, &mImpl->device) != MA_SUCCESS)
  {
    std::println("[Voice] Failed to initialise playback device.");
    return false;
  }
  mImpl->deviceInitialised = true;

  if (ma_device_start(&mImpl->device) != MA_SUCCESS)
  {
    std::println("[Voice] Failed to start playback device.");
    ma_device_uninit(&mImpl->device);
    mImpl->deviceInitialised = false;
    return false;
  }

  mImpl->playing = true;
  std::println("[Voice] Playback started ({}, {} Hz, {} ch, period {} frames).",
               headless ? "headless" : mImpl->device.playback.name,
               mImpl->device.sampleRate, mImpl->device.playback.channels,
               mImpl->device.playback.internalPeriodSizeInFrames);
  return true;
}

void VoicePlayer::Stop()
{
  if (!mImpl->deviceInitialised)
    return;

  ma_device_uninit(&mImpl->device); // stops the device and joins its thread
  mImpl->deviceInitialised = false;
  mImpl->playing = false;

  for (const auto& s : mImpl->senders)
  {
    if (!s.inUse)
      continue;
    const auto& st = s.jitter.GetStats();
    std::println("[Voice] Sender {:08x}: played {}, FEC {}, PLC {}, late {}, dropped {}, target {} ms.",
                 s.senderId, st.played, st.recovered, st.concealed, st.late, st.dropped,
                 s.jitter.TargetFrames() * Voice::kFrameMs);
  }
  if (const int drops = mImpl->inboxDrops.exchange(0); drops > 0)
    std::println("[Voice] {} incoming packet(s) dropped (inbox full).", drops);

  mImpl->ResetSenders();
  std::println("[Voice] Playback stopped.");
}

void VoicePlayer::PushPacket(std::uint32_t senderId, std::uint32_t seq,
                             std::span<const std::uint8_t> opus)
{
  if (!mImpl->playing || opus.empty() || opus.size() > Voice::kMaxEncodedBytes)
    return;

  IncomingPacket pkt;
  pkt.senderId  = senderId;
  pkt.seq       = seq;
  pkt.arrivalUs = NowUs();
  pkt.size      = static_cast<std::uint16_t>(opus.size());
  std::memcpy(pkt.data.data(), opus.data(), opus.size());

  if (!mImpl->inbox.push(pkt))
    ++mImpl->inboxDrops;
}

void VoicePlayer::SetOutputTap(OutputTap tap)
{
  mImpl->tap = std::move(tap);
}

void VoicePlayer::SetVolume(float volume)
{
  mImpl->volume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

float VoicePlayer::Volume() const
{
  return mImpl->volume.load(std::memory_order_relaxed);
}
