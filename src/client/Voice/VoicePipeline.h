#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <array>

#include "../../common/Event.h"
#include "../../common/SpscRingBuffer.h"
#include "../../common/VoicePacket.h"
#include "VoicePipelineOptions.h"

class EnetClient;
class VoiceCapture;
class VoiceEncoder;
class VoicePlayer;
class WavPlaybackSource;
class WavFileRecorder;
struct ClientConfig;

// Owns every voice sub-component and orchestrates the full pipeline:
//
//   Capture → Encode → Send (via EnetClient)
//   Receive → Jitter buffer → Decode → Play (all on the audio output clock)
//
// Create once; call Start() after joining a room and Stop() on leaving.
class VoicePipeline
{
public:
  explicit VoicePipeline(EnetClient& enetClient,
                         VoicePipelineOptions opts = {});
  ~VoicePipeline();

  VoicePipeline(const VoicePipeline&) = delete;
  VoicePipeline& operator=(const VoicePipeline&) = delete;

  // Initialise all sub-components (capture device, encoder, decoder, player).
  bool Init();

  // Apply voice-related settings from the client configuration
  // (volume, VAD threshold, noise filter, …).  Safe to call at any time.
  void ApplyConfig(const ClientConfig& config);

  // Start capturing + playback.
  bool Start();

  // Stop capturing + playback and reset state.
  void Stop();

  bool IsRunning() const;

  // Select the input/output devices by name (empty = system default).  If
  // the pipeline is running, only the affected device is reopened, so an
  // active voice session continues.  Ignored for the side replaced by a
  // -playback / -capture file.
  void SetAudioDevices(const std::string& inputDevice, const std::string& outputDevice);

  // Watchdog for runtime device changes; call every ~2 s while the app runs,
  // from the thread that may open audio devices (the GUI thread), never from
  // an audio callback.  Reopens a device that stopped unexpectedly (e.g. an
  // unplugged headset), which falls back to the system default, and switches
  // back once the selected device is connected again.
  void CheckAudioDevices();

  // Called by the VoiceConnector when a voice frame arrives from the server.
  void HandleIncomingVoiceFrame(const std::uint8_t* data, std::size_t size);

  // Fired when a valid voice frame from a remote user has been decoded.
  // Args: sender user name
  Event<std::string> onSpeakerActivity;

  // Microphone mute
  void SetMuted(bool muted);
  bool IsMuted() const;

  // Store the local user's name so the encoder worker can fire onSpeakerActivity
  // for the local client.  Call once after a successful room entry.
  void SetLocalUserName(std::string name);

  // Output volume (0.0 – 1.0)
  void SetVolume(float volume);
  float Volume() const;

  // VAD gate: frames whose RMS energy is below this threshold are not
  // transmitted. Legacy compatibility alias for min RMS floor.
  void  SetVadThreshold(float threshold);
  float VadThreshold() const;

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

  // Noise filter: enables/disables the high-pass IIR filter (~80 Hz) inside
  // VoiceCapture to remove low-frequency rumble before encoding.
  void SetNoiseFilter(bool enabled);
  bool IsNoiseFilterEnabled() const;

  // Input gain: microphone amplification factor (0.0-2.0, 1.0 = unity).
  // Delegates to VoiceCapture; safe to call at any time.
  void  SetInputGain(float gain);
  float InputGain() const;

  // Returns the RMS level of the last captured audio frame (post-gain,
  // post-filter).  Returns 0 if the pipeline is not running.
  float GetInputRms() const;

private:
  EnetClient&            mEnetClient;
  VoicePipelineOptions   mOptions;

  std::unique_ptr<VoiceCapture>      mCapture;
  std::unique_ptr<VoiceEncoder>      mEncoder;
  std::unique_ptr<VoicePlayer>       mPlayer;

  // Optional overrides set via CLI flags.
  std::unique_ptr<WavPlaybackSource> mWavSource;   // -playback <file>
  std::unique_ptr<WavFileRecorder>   mWavRecorder; // -capture  <file>

  std::uint32_t mSequenceNumber{0};
  bool          mRunning{false};

  // Serialises Start/Stop and device switches, which may be triggered from
  // the network thread, the settings dialog and the device watchdog.
  std::mutex mDeviceMutex;

  // Device watchdog state (see CheckAudioDevices).
  struct DeviceWatch
  {
    bool selectedWasPresent{true}; // switch back only when it (re)appears
  };
  DeviceWatch mInputWatch;
  DeviceWatch mOutputWatch;
  unsigned    mDeviceCheckCount{0};

  // Local user name — written from the main thread (SetLocalUserName),
  // read from the encoder worker thread.  Protected by mLocalUserNameMutex.
  std::string mLocalUserName;
  std::mutex  mLocalUserNameMutex;

  // ---------------------------------------------------------------------------
  // Capture → Encoder pipeline
  // Audio callback is the sole producer; encoder worker is the sole consumer.
  // SPSC ring avoids any mutex on the audio thread.
  // ---------------------------------------------------------------------------
  SpscRingBuffer<std::array<float, Voice::kFrameSize>, 128> mCaptureRing;
  // Condition variable used only to wake the encoder worker when the ring has
  // new data.  The cv mutex is never held by the audio callback itself.
  std::mutex              mEncoderCvMutex;
  std::condition_variable mEncoderCv;
  std::thread             mWorkerThread;
  std::atomic<bool>       mWorkerRunning{false};
  std::atomic<int>        mCaptureDrop{0};  // frames dropped because ring was full

  // Throttles onSpeakerActivity so the GUI is updated at most every
  // kSpeakingEventInterval per user instead of for every 20 ms frame.
  void EmitSpeakerActivity(const std::string& userName);
  static constexpr auto kSpeakingEventInterval = std::chrono::milliseconds(100);
  std::mutex mSpeakingMutex;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> mLastSpeakingEvent;
};
