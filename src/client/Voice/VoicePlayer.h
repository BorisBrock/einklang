#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

#include "AudioDevices.h"

// Plays back voice received from remote peers using miniaudio.
//
// Encoded Opus packets are handed over from the network thread through a
// lock-free queue.  The audio output callback owns everything else: it sorts
// packets into one adaptive JitterBuffer per sender, decodes exactly as much
// audio as the device consumes, and mixes all speakers together.  Driving
// decode from the output clock (instead of decoding on packet arrival) is
// what keeps playback smooth under network jitter.
class VoicePlayer
{
public:
  // Optional sink receiving the final mixed output (mono, 48 kHz).
  using OutputTap = std::function<void(std::span<const float> pcm)>;

  VoicePlayer();
  ~VoicePlayer();

  VoicePlayer(const VoicePlayer&) = delete;
  VoicePlayer& operator=(const VoicePlayer&) = delete;

  // Start playback.  With `headless` set no audio device is opened; instead a
  // real-time paced null device drives the mixer and the result is only
  // delivered to the output tap (used for -capture <file>).
  bool Start(bool headless = false);
  void Stop();

  // Select the output device by name; empty = system default.  Takes effect
  // on the next Start().  If the device is not present at that point (e.g. an
  // unplugged headset), the system default is used instead.
  // Returns true if the selection changed.
  bool SetDeviceName(std::string name);
  std::string DeviceName() const;

  bool IsPlaying() const;

  // True while playing on the system default because the selected device
  // was unavailable at Start().
  bool IsOnFallbackDevice() const;

  // Reports whether the running device was lost (e.g. unplugged) or rerouted
  // since the last call.  Call periodically from a control thread, never
  // from the audio thread; on Lost, Stop() + Start() reopens a device.
  AudioDevices::DeviceEvent PollDeviceEvent();

  // Queue an encoded packet from a remote sender.  Must only be called from
  // one thread (the network thread).  Never blocks.
  void PushPacket(std::uint32_t senderId, std::uint32_t seq, std::span<const std::uint8_t> opus);

  void SetOutputTap(OutputTap tap);

  // Set playback volume (0.0 – 1.0, default 1.0).
  void SetVolume(float volume);
  float Volume() const;

  // Opaque implementation — public only so the C callback can access it.
  struct Impl;

private:
  std::unique_ptr<Impl> mImpl;
};
