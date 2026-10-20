#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>
#include <atomic>

#include "AudioDevices.h"
#include "VoiceActivityDetector.h"

// Captures PCM audio from an input device using miniaudio.
// Delivers fixed-size frames (960 samples @ 48 kHz mono = 20 ms) via a
// callback on the audio thread.
class VoiceCapture
{
public:
  // Called on the audio thread with exactly kFrameSize float samples.
  using FrameCallback = std::function<void(std::span<const float> pcm)>;

  VoiceCapture();
  ~VoiceCapture();

  // Non-copyable, non-movable (owns the miniaudio device handle)
  VoiceCapture(const VoiceCapture&) = delete;
  VoiceCapture& operator=(const VoiceCapture&) = delete;

  // Initialise the capture device.  Returns false on failure.
  bool Init();

  // Start / stop audio capture.
  bool Start();
  void Stop();

  bool IsCapturing() const;

  // Select the input device by name; empty = system default.  Takes effect
  // on the next Start().  If the device is not present at that point (e.g. an
  // unplugged headset), the system default is used instead.
  // Returns true if the selection changed.
  bool SetDeviceName(std::string name);
  std::string DeviceName() const;

  // True while capturing from the system default because the selected
  // device was unavailable at Start().
  bool IsOnFallbackDevice() const;

  // Reports whether the running device was lost (e.g. unplugged) or rerouted
  // since the last call.  Call periodically from a control thread, never
  // from the audio thread; on Lost, Stop() + Start() reopens a device.
  AudioDevices::DeviceEvent PollDeviceEvent();

  // Set the callback that receives complete PCM frames.
  void SetFrameCallback(FrameCallback cb);

  // Mute: silently drops captured frames without invoking the callback.
  void SetMuted(bool muted);
  bool IsMuted() const;

  // Legacy compatibility alias for the old threshold-based gate.
  // Internally maps to min RMS floor in the adaptive VAD.
  void  SetVadThreshold(float threshold);
  float VadThreshold() const;

  // Adaptive VAD tuning parameters.
  void  SetVadSnrFactor(float snrFactor);
  float VadSnrFactor() const;

  void SetVadAttackFrames(int frames);
  int  VadAttackFrames() const;

  void SetVadReleaseFrames(int frames);
  int  VadReleaseFrames() const;

  void  SetVadZcrRange(float minPerSecond, float maxPerSecond);
  float VadZcrMin() const;
  float VadZcrMax() const;

  void  SetVadMinRmsFloor(float floor);
  float VadMinRmsFloor() const;

  // Noise filter: if enabled, a first-order high-pass IIR filter (~80 Hz
  // cutoff) is applied to each captured frame before the VAD check, removing
  // DC offset and low-frequency rumble (fan noise, desk vibration, etc.).
  void SetNoiseFilter(bool enabled);
  bool IsNoiseFilterEnabled() const;

  // Input gain: multiplier applied after the noise filter and before the VAD
  // gate.  1.0 = unity gain; typical range 0.0-2.0.
  void  SetInputGain(float gain);
  float InputGain() const;

  // Returns the RMS energy of the most recently processed frame (post-gain,
  // post-filter, before the VAD gate).  Thread-safe; safe to poll from any
  // thread, e.g. for a live level-meter in the settings UI.
  float GetLastRms() const;

  // Opaque implementation — public only so the C callback can access it.
  struct Impl;
  Impl* mImpl{};
};
