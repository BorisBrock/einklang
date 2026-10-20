#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "VoiceCapture.h"
#include "AudioDevicesInternal.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <print>
#include <vector>

#include "../../common/VoicePacket.h"

// ---------------------------------------------------------------------------
// Private implementation (hides the miniaudio types from the header)
// ---------------------------------------------------------------------------

// High-pass filter alpha for ~80 Hz cutoff at 48 kHz:
//   alpha = RC / (RC + T),  RC = 1/(2π·fc),  T = 1/fs
static constexpr float kHpfAlpha = 0.9896f;

struct VoiceCapture::Impl
{
  ma_device        device{};
  bool             deviceInitialised{false};
  bool             capturing{false};
  std::atomic<bool> muted{false};

  // Selected input device name (empty = system default).  Start() may run on
  // a different thread than the settings dialog, hence the mutex.
  std::mutex       deviceNameMutex;
  std::string      deviceName;

  // True if the selected device was unavailable at Start() and the system
  // default was opened instead.  Control thread only.
  bool             onFallback{false};

  AudioDevices::DeviceHealth health;

  // Legacy compatibility: maps to adaptive VAD minimum RMS floor.
  // Adaptive VAD tunables.
  std::atomic<float> vadSnrFactor{4.0f};
  std::atomic<int>   vadAttackFrames{2};
  std::atomic<int>   vadReleaseFrames{20};
  std::atomic<float> vadZcrMin{500.0f};
  std::atomic<float> vadZcrMax{5000.0f};
  std::atomic<float> vadMinRmsFloor{0.005f};

  // Noise filter: first-order IIR high-pass (~80 Hz cutoff) to remove rumble.
  std::atomic<bool> noiseFilter{false};

  // Input gain: applied after the noise filter, before VAD (default = 1.0).
  std::atomic<float> inputGain{1.0f};

  // RMS energy of the last processed frame (post-gain, post-filter).
  // Updated on the audio thread; read from any thread for the level meter.
  std::atomic<float> lastRms{0.0f};

  VoiceActivityDetector vad;

  // Scratch buffers reused on the audio thread to avoid per-frame allocations.
  std::array<float, Voice::kFrameSize> normalizedFrame{};
  std::array<std::array<float, Voice::kFrameSize>, 3> vadOutFrames{};

  VoiceCapture::FrameCallback frameCallback;

  // Accumulator: miniaudio may deliver frames in arbitrary sizes, so we
  // collect samples until we have a full 20 ms frame.  Fixed-size so the
  // audio thread never allocates.
  std::array<float, Voice::kFrameSize> accumulator{};
  std::size_t accumulated{0};

  // HPF state — only touched on the audio thread, no atomic needed.
  float hpPrevIn{0.0f};
  float hpPrevOut{0.0f};
};

// ---------------------------------------------------------------------------
// miniaudio data callback (runs on the audio thread)
// ---------------------------------------------------------------------------
static void captureCallback(ma_device* pDevice, void* /*pOutput*/,
                            const void* pInput, ma_uint32 frameCount)
{
  auto* impl = static_cast<VoiceCapture::Impl*>(pDevice->pUserData);
  if (!impl)
    return;
  impl->health.OnDataCallback();
  if (!impl->frameCallback)
    return;

  const bool muted = impl->muted.load(std::memory_order_relaxed);

  const auto* input = static_cast<const float*>(pInput);
  constexpr std::size_t frameSize = static_cast<std::size_t>(Voice::kFrameSize);

  std::size_t consumed = 0;
  while (consumed < frameCount)
  {
    const std::size_t n = std::min<std::size_t>(frameCount - consumed, frameSize - impl->accumulated);
    std::memcpy(impl->accumulator.data() + impl->accumulated, input + consumed, n * sizeof(float));
    impl->accumulated += n;
    consumed += n;

    if (impl->accumulated < frameSize)
      break;
    impl->accumulated = 0;

    std::span<float> frame(impl->accumulator.data(), frameSize);

    // -----------------------------------------------------------------------
    // 1. Noise filter: first-order IIR high-pass (~80 Hz cutoff)
    //    y[n] = alpha * (y[n-1] + x[n] - x[n-1])
    //    Removes DC offset, fan hum, and low-frequency rumble.
    // -----------------------------------------------------------------------
    if (impl->noiseFilter.load(std::memory_order_relaxed))
    {
      for (std::size_t i = 0; i < frameSize; ++i)
      {
        const float x      = frame[i];
        const float y      = kHpfAlpha * (impl->hpPrevOut + x - impl->hpPrevIn);
        impl->hpPrevIn  = x;
        impl->hpPrevOut = y;
        frame[i] = y;
      }
    }

    // -----------------------------------------------------------------------
    // 2. Input gain: amplify the (filtered) signal before VAD.
    // -----------------------------------------------------------------------
    const float gain = impl->inputGain.load(std::memory_order_relaxed);
    if (gain != 1.0f)
    {
      for (std::size_t i = 0; i < frameSize; ++i)
        frame[i] *= gain;
    }

    // -----------------------------------------------------------------------
    // 3. Build a normalised view for feature extraction.
    // -----------------------------------------------------------------------
    for (std::size_t i = 0; i < frameSize; ++i)
      impl->normalizedFrame[i] = std::clamp(frame[i], -1.0f, 1.0f);

    VoiceActivityDetectorConfig vadConfig;
    vadConfig.snrFactor = impl->vadSnrFactor.load(std::memory_order_relaxed);
    vadConfig.attackFrames = impl->vadAttackFrames.load(std::memory_order_relaxed);
    vadConfig.releaseFrames = impl->vadReleaseFrames.load(std::memory_order_relaxed);
    vadConfig.zcrMin = impl->vadZcrMin.load(std::memory_order_relaxed);
    vadConfig.zcrMax = impl->vadZcrMax.load(std::memory_order_relaxed);
    vadConfig.minRmsFloor = impl->vadMinRmsFloor.load(std::memory_order_relaxed);
    vadConfig.noiseWindowFrames = 100;
    vadConfig.preRollFrames = 2;

    const auto vadResult = impl->vad.ProcessFrame(
        std::span<const float>(impl->normalizedFrame.data(), frameSize),
        vadConfig,
        impl->vadOutFrames);

    // RMS is always published for the live meter, independent of VAD state.
    impl->lastRms.store(vadResult.features.rms, std::memory_order_relaxed);

    // Muted: all processing still runs, but frames are not forwarded.
    if (!muted)
    {
      for (std::size_t i = 0; i < vadResult.outputFrameCount; ++i)
        impl->frameCallback(std::span<const float>(impl->vadOutFrames[i].data(), frameSize));
    }
  }
}

// Device state changes (backend thread).  Only flags the change; the device
// is reopened from a control thread via PollDeviceEvent().
static void captureNotification(const ma_device_notification* pNotification)
{
  if (auto* impl = static_cast<VoiceCapture::Impl*>(pNotification->pDevice->pUserData))
    impl->health.OnNotification(pNotification->type);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
VoiceCapture::VoiceCapture()
  : mImpl(new Impl)
{
}

VoiceCapture::~VoiceCapture()
{
  Stop();
  delete mImpl;
}

bool VoiceCapture::Init()
{
  // Nothing to do here: the audio device is intentionally created in Start()
  // so that ma_device_init (which calls into WASAPI and touches COM) does not
  // run before the WebView2 host has established its COM STA context.
  return true;
}

bool VoiceCapture::Start()
{
  if (mImpl->capturing)
    return true;

  // Initialise (or re-initialise) the device on every Start() call so that
  // the WASAPI COM call always happens inside the WebView2 message loop.
  if (!mImpl->deviceInitialised)
  {
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format   = ma_format_f32;
    config.capture.channels = Voice::kChannels;
    config.sampleRate       = Voice::kSampleRate;
    config.dataCallback         = captureCallback;
    config.notificationCallback = captureNotification;
    config.pUserData            = mImpl;

    std::string deviceName;
    {
      std::lock_guard lock(mImpl->deviceNameMutex);
      deviceName = mImpl->deviceName;
    }

    // Prefer the selected device, but fall back to the system default if it
    // is gone or refuses to open, so voice keeps working.
    ma_device_id deviceId{};
    bool initialised = false;
    if (!deviceName.empty())
    {
      if (AudioDevices::FindDeviceId(ma_device_type_capture, deviceName, deviceId))
      {
        config.capture.pDeviceID = &deviceId;
        initialised = ma_device_init(nullptr, &config, &mImpl->device) == MA_SUCCESS;
        if (!initialised)
          std::println("[Voice] Failed to open input device '{}'; using system default.", deviceName);
      }
      else
      {
        std::println("[Voice] Input device '{}' not found; using system default.", deviceName);
      }
    }

    if (!initialised)
    {
      config.capture.pDeviceID = nullptr;
      if (ma_device_init(nullptr, &config, &mImpl->device) != MA_SUCCESS)
      {
        std::println("[Voice] Failed to initialise capture device.");
        return false;
      }
    }

    mImpl->onFallback = !deviceName.empty() && !initialised;
    mImpl->deviceInitialised = true;
    std::println("[Voice] Capture device initialised ({}, {}Hz, mono).",
                 mImpl->device.capture.name, Voice::kSampleRate);
  }

  mImpl->health.OnStarting();
  if (ma_device_start(&mImpl->device) != MA_SUCCESS)
  {
    std::println("[Voice] Failed to start capture device.");
    ma_device_uninit(&mImpl->device);
    mImpl->deviceInitialised = false;
    return false;
  }

  mImpl->accumulated = 0;
  mImpl->vad.Reset();

  mImpl->capturing = true;
  std::println("[Voice] Capture started.");
  return true;
}

void VoiceCapture::Stop()
{
  if (!mImpl->deviceInitialised || !mImpl->capturing)
    return;

  mImpl->health.OnStopping();
  ma_device_stop(&mImpl->device);
  ma_device_uninit(&mImpl->device);   // release the WASAPI/COM resources
  mImpl->deviceInitialised = false;
  mImpl->capturing = false;
  std::println("[Voice] Capture stopped.");
}

bool VoiceCapture::IsCapturing() const
{
  return mImpl && mImpl->capturing;
}

bool VoiceCapture::SetDeviceName(std::string name)
{
  std::lock_guard lock(mImpl->deviceNameMutex);
  if (mImpl->deviceName == name)
    return false;
  mImpl->deviceName = std::move(name);
  return true;
}

std::string VoiceCapture::DeviceName() const
{
  std::lock_guard lock(mImpl->deviceNameMutex);
  return mImpl->deviceName;
}

bool VoiceCapture::IsOnFallbackDevice() const
{
  return mImpl->capturing && mImpl->onFallback;
}

AudioDevices::DeviceEvent VoiceCapture::PollDeviceEvent()
{
  if (!mImpl->capturing)
    return AudioDevices::DeviceEvent::None;
  return mImpl->health.Poll();
}

void VoiceCapture::SetFrameCallback(FrameCallback cb)
{
  mImpl->frameCallback = std::move(cb);
}

void VoiceCapture::SetMuted(bool muted)
{
  mImpl->muted.store(muted, std::memory_order_relaxed);
}

bool VoiceCapture::IsMuted() const
{
  return mImpl->muted.load(std::memory_order_relaxed);
}

void VoiceCapture::SetVadThreshold(float threshold)
{
  // Legacy alias: the threshold is used as the adaptive VAD's minimum RMS floor.
  SetVadMinRmsFloor(threshold);
}

float VoiceCapture::VadThreshold() const
{
  return VadMinRmsFloor();
}

void VoiceCapture::SetVadSnrFactor(float snrFactor)
{
  mImpl->vadSnrFactor.store(snrFactor, std::memory_order_relaxed);
}

float VoiceCapture::VadSnrFactor() const
{
  return mImpl->vadSnrFactor.load(std::memory_order_relaxed);
}

void VoiceCapture::SetVadAttackFrames(int frames)
{
  mImpl->vadAttackFrames.store(std::max(1, frames), std::memory_order_relaxed);
}

int VoiceCapture::VadAttackFrames() const
{
  return mImpl->vadAttackFrames.load(std::memory_order_relaxed);
}

void VoiceCapture::SetVadReleaseFrames(int frames)
{
  mImpl->vadReleaseFrames.store(std::max(1, frames), std::memory_order_relaxed);
}

int VoiceCapture::VadReleaseFrames() const
{
  return mImpl->vadReleaseFrames.load(std::memory_order_relaxed);
}

void VoiceCapture::SetVadZcrRange(float minPerSecond, float maxPerSecond)
{
  if (minPerSecond > maxPerSecond)
    std::swap(minPerSecond, maxPerSecond);
  mImpl->vadZcrMin.store(minPerSecond, std::memory_order_relaxed);
  mImpl->vadZcrMax.store(maxPerSecond, std::memory_order_relaxed);
}

float VoiceCapture::VadZcrMin() const
{
  return mImpl->vadZcrMin.load(std::memory_order_relaxed);
}

float VoiceCapture::VadZcrMax() const
{
  return mImpl->vadZcrMax.load(std::memory_order_relaxed);
}

void VoiceCapture::SetVadMinRmsFloor(float floor)
{
  const float clamped = std::max(0.0f, floor);
  mImpl->vadMinRmsFloor.store(clamped, std::memory_order_relaxed);
}

float VoiceCapture::VadMinRmsFloor() const
{
  return mImpl->vadMinRmsFloor.load(std::memory_order_relaxed);
}

void VoiceCapture::SetNoiseFilter(bool enabled)
{
  // Note: hpPrevIn / hpPrevOut are not reset here to avoid touching audio-
  // thread state from another thread.  The HPF converges to the new signal
  // within one frame (~20 ms), so this is imperceptible.
  mImpl->noiseFilter.store(enabled, std::memory_order_relaxed);
}

bool VoiceCapture::IsNoiseFilterEnabled() const
{
  return mImpl->noiseFilter.load(std::memory_order_relaxed);
}

void VoiceCapture::SetInputGain(float gain)
{
  mImpl->inputGain.store(gain, std::memory_order_relaxed);
}

float VoiceCapture::InputGain() const
{
  return mImpl->inputGain.load(std::memory_order_relaxed);
}

float VoiceCapture::GetLastRms() const
{
  return mImpl->lastRms.load(std::memory_order_relaxed);
}