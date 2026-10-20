#pragma once

#include <atomic>
#include <functional>
#include <span>
#include <string>
#include <thread>

// Replaces VoiceCapture when -playback <file> is specified.
//
// Reads any audio file supported by miniaudio (WAV, FLAC, MP3, OGG, …) and
// resamples it to 48 kHz mono f32 on the fly.  Delivers fixed 20 ms frames
// (960 samples) at real-time pace via the same FrameCallback interface that
// VoicePipeline wires to its encoder worker.  Loops the file indefinitely.
class WavPlaybackSource
{
public:
  // Matches VoiceCapture::FrameCallback so VoicePipeline can treat both uniformly.
  using FrameCallback = std::function<void(std::span<const float> pcm)>;

  WavPlaybackSource();
  ~WavPlaybackSource();

  WavPlaybackSource(const WavPlaybackSource&) = delete;
  WavPlaybackSource& operator=(const WavPlaybackSource&) = delete;

  // Validate that the file exists and can be decoded; does not start playback.
  bool Init(const std::string& filePath);

  bool Start();
  void Stop();
  bool IsRunning() const;

  void SetFrameCallback(FrameCallback cb);

  // ----- Interface stubs matching VoiceCapture so VoicePipeline delegates compile -----

  // Mute: when muted the callback is not fired (frames are still read and
  // timed to maintain the loop position, but silence is sent).
  void SetMuted(bool muted);
  bool IsMuted() const;

  // Input gain: multiplier applied to every sample before delivery.
  void  SetInputGain(float gain);
  float InputGain() const;

  // VAD / noise-filter are no-ops for a file source.
  void  SetVadThreshold(float) {}
  float VadThreshold()         const { return 0.f; }
  void  SetNoiseFilter(bool)   {}
  bool  IsNoiseFilterEnabled() const { return false; }

  // RMS of the last delivered frame (useful for the existing level-meter code).
  float GetLastRms() const;

private:
  void workerThreadFunc();

  std::string    mFilePath;
  FrameCallback  mCallback;
  std::thread    mThread;

  std::atomic<bool>  mRunning{false};
  std::atomic<bool>  mMuted{false};
  std::atomic<float> mGain{1.0f};
  std::atomic<float> mLastRms{0.0f};
};
