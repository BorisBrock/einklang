#include "VoicePipeline.h"

#include <cstdint>
#include <cstring>
#include <print>
#include <string>
#include <vector>

#include "../../common/VoicePacket.h"
#include "../Configuration/ClientConfig.h"
#include "../Networking/ClientPacketFactory.h"
#include "../Networking/EnetClient.h"
#include "VoiceCapture.h"
#include "VoiceEncoder.h"
#include "VoicePlayer.h"
#include "WavPlaybackSource.h"
#include "WavFileRecorder.h"

VoicePipeline::VoicePipeline(EnetClient& enetClient, VoicePipelineOptions opts)
  : mEnetClient(enetClient),
    mOptions(std::move(opts)),
    mCapture(std::make_unique<VoiceCapture>()),
    mEncoder(std::make_unique<VoiceEncoder>()),
    mPlayer(std::make_unique<VoicePlayer>())
{
  if (!mOptions.playbackFile.empty())
    mWavSource = std::make_unique<WavPlaybackSource>();
  if (!mOptions.captureFile.empty())
    mWavRecorder = std::make_unique<WavFileRecorder>();
}

VoicePipeline::~VoicePipeline()
{
  Stop();
}

void VoicePipeline::ApplyConfig(const ClientConfig& config)
{
  SetVolume(config.outputVolume);
  SetVadMinRmsFloor(config.vadMinRmsFloor);
  SetVadSnrFactor(config.vadSnrFactor);
  SetVadAttackFrames(config.vadAttackFrames);
  SetVadReleaseFrames(config.vadReleaseFrames);
  SetVadZcrRange(config.vadZcrMin, config.vadZcrMax);
  SetNoiseFilter(config.noiseFilter);
  SetInputGain(config.inputGain);
  std::println("[Voice] Config applied — vol: {:.2f}, vad floor: {:.4f}, snr: {:.2f}, atk: {}, rel: {}, zcr: [{:.0f}, {:.0f}], noise filter: {}, input gain: {:.2f}.",
               config.outputVolume, config.vadMinRmsFloor, config.vadSnrFactor,
               config.vadAttackFrames, config.vadReleaseFrames,
               config.vadZcrMin, config.vadZcrMax,
               config.noiseFilter ? "on" : "off",
               config.inputGain);
}

bool VoicePipeline::Init()
{
  // NOTE: audio device creation is intentionally deferred to Start() to avoid
  // touching COM before WebView2 has established its STA context.
  // Only the Opus encoder is initialised here — it is pure CPU, no OS deps.
  if (!mEncoder->Init())  return false;

  // Validate WAV recorder output file up-front.  In -capture mode the mixed
  // output of the player is written to the file instead of a speaker.
  if (mWavRecorder)
  {
    if (!mWavRecorder->Open(mOptions.captureFile))
      return false;
    mPlayer->SetOutputTap([this](std::span<const float> pcm) { mWavRecorder->WriteSamples(pcm); });
  }

  // Validate WAV playback source up-front.
  if (mWavSource)
  {
    if (!mWavSource->Init(mOptions.playbackFile))
      return false;
  }

  // Determine which audio source provides frames to the encoder worker.
  // The callback is identical regardless of source: push into the SPSC ring
  // and wake the encoder worker.
  auto captureFrameCallback = [this](std::span<const float> pcm)
  {
    std::array<float, Voice::kFrameSize> frame;
    std::memcpy(frame.data(), pcm.data(), Voice::kFrameSize * sizeof(float));
    if (!mCaptureRing.push(frame))
      ++mCaptureDrop;
    mEncoderCv.notify_one();
  };

  if (mWavSource)
    mWavSource->SetFrameCallback(std::move(captureFrameCallback));
  else
    mCapture->SetFrameCallback(std::move(captureFrameCallback));

  std::println("[Voice] Pipeline initialised{}{}.",
               mWavSource   ? " (playback: "  + mOptions.playbackFile + ")" : "",
               mWavRecorder ? " (capture: "   + mOptions.captureFile  + ")" : "");
  return true;
}

bool VoicePipeline::Start()
{
  if (mRunning)
    return true;

  // In -capture mode no speaker is opened; a headless device paces the
  // mixer in real time and its output goes to the WAV file.
  if (!mPlayer->Start(/*headless=*/mWavRecorder != nullptr))
    return false;

  // --- Encoder worker ---------------------------------------------------
  // Reads frames from the SPSC capture ring and encodes + sends them.
  // The audio callback never holds any lock — it only writes to the ring.
  mWorkerRunning.store(true);
  mWorkerThread = std::thread([this]()
  {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(Voice::kMaxEncodedBytes);

    while (mWorkerRunning.load(std::memory_order_relaxed))
    {
      std::array<float, Voice::kFrameSize> frame;
      if (!mCaptureRing.pop(frame))
      {
        // Ring is empty — wait up to 5 ms for a new frame notification.
        std::unique_lock<std::mutex> lock(mEncoderCvMutex);
        mEncoderCv.wait_for(lock, std::chrono::milliseconds(5));
        continue;
      }

      const int bytes = mEncoder->Encode(
          std::span<const float>(frame.data(), frame.size()), encoded);
      if (bytes <= 0)
        continue;

      // Not connected (e.g. settings level-meter preview): nothing to send.
      if (!mEnetClient.IsConnected())
        continue;

      auto packet = ClientPacketFactory::VoiceFrame(mSequenceNumber++, encoded);
      mEnetClient.EnqueuePacket(packet, Voice::kVoiceChannel, /*flags=*/0);

      // Emit local speaking indicator so the sender also sees their own VAD dot.
      std::string localName;
      {
        std::lock_guard<std::mutex> lock(mLocalUserNameMutex);
        localName = mLocalUserName;
      }
      if (!localName.empty())
        EmitSpeakerActivity(localName);
    }

    const int drops = mCaptureDrop.load(std::memory_order_relaxed);
    if (drops > 0)
      std::println("[Voice] Encoder worker exiting — {} capture frame(s) dropped (ring full).", drops);
  });

  // Start the audio source (WAV file or real microphone).
  const bool captureOk = mWavSource ? mWavSource->Start() : mCapture->Start();
  if (!captureOk)
  {
    mWorkerRunning.store(false);
    mEncoderCv.notify_one();
    if (mWorkerThread.joinable()) mWorkerThread.join();
    mPlayer->Stop();
    return false;
  }

  mRunning = true;
  std::println("[Voice] Pipeline started.");
  return true;
}

void VoicePipeline::Stop()
{
  if (!mRunning)
    return;

  // Stop the audio source first so no more frames enter the capture ring.
  if (mWavSource)
    mWavSource->Stop();
  else
    mCapture->Stop();

  // Signal and join the encoder worker.
  mWorkerRunning.store(false);
  mEncoderCv.notify_one();
  if (mWorkerThread.joinable())
    mWorkerThread.join();

  // Drain frames captured but not yet encoded.
  std::array<float, Voice::kFrameSize> discard;
  while (mCaptureRing.pop(discard)) {}

  mPlayer->Stop();

  // Finalise capture WAV (flushes and writes the RIFF header) before resetting.
  if (mWavRecorder)
    mWavRecorder->Close();

  mSequenceNumber = 0;
  mCaptureDrop.store(0);
  mRunning = false;
  std::println("[Voice] Pipeline stopped.");
}

bool VoicePipeline::IsRunning() const
{
  return mRunning;
}

void VoicePipeline::HandleIncomingVoiceFrame(const std::uint8_t* data, std::size_t size)
{
  // Called on the ENet event-loop thread.  Parse the relay header and hand
  // the encoded payload to the player; decoding happens on the audio thread,
  // paced by the output device clock.

  if (size < Voice::kRelayMinPayloadSize)
    return;

  // Parse relay metadata: [senderPeerId][senderNameLen][senderName][sequenceNumber][opusPayload]
  std::uint32_t senderPeerId = 0;
  std::memcpy(&senderPeerId, data, sizeof(senderPeerId));

  const std::size_t nameLenOffset = Voice::kRelaySenderPeerIdSize;
  const auto senderNameLen = static_cast<std::size_t>(data[nameLenOffset]);
  const std::size_t relayHeaderSize = Voice::kRelayPrefixSize + senderNameLen + Voice::kHeaderSize;
  if (size <= relayHeaderSize)
    return;

  std::uint32_t seqNum = 0;
  std::memcpy(&seqNum, data + Voice::kRelayPrefixSize + senderNameLen, sizeof(seqNum));

  mPlayer->PushPacket(senderPeerId, seqNum, {data + relayHeaderSize, size - relayHeaderSize});

  if (senderNameLen > 0)
  {
    const char* namePtr = reinterpret_cast<const char*>(data + Voice::kRelayPrefixSize);
    EmitSpeakerActivity(std::string(namePtr, senderNameLen));
  }
}

void VoicePipeline::EmitSpeakerActivity(const std::string& userName)
{
  const auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(mSpeakingMutex);
    auto& last = mLastSpeakingEvent[userName];
    if (now - last < kSpeakingEventInterval)
      return;
    last = now;
  }
  onSpeakerActivity.emit(userName);
}

void VoicePipeline::SetMuted(bool muted)
{
  if (mWavSource)
    mWavSource->SetMuted(muted);
  else
    mCapture->SetMuted(muted);
}

void VoicePipeline::SetLocalUserName(std::string name)
{
  std::lock_guard<std::mutex> lock(mLocalUserNameMutex);
  mLocalUserName = std::move(name);
}

bool VoicePipeline::IsMuted() const
{
  return mWavSource ? mWavSource->IsMuted() : mCapture->IsMuted();
}

void VoicePipeline::SetVolume(float volume)
{
  mPlayer->SetVolume(volume);
}

float VoicePipeline::Volume() const
{
  return mPlayer->Volume();
}

void VoicePipeline::SetVadThreshold(float threshold)
{
  mCapture->SetVadThreshold(threshold); // no-op when mWavSource is active
}

float VoicePipeline::VadThreshold() const
{
  return mCapture->VadThreshold();
}

void VoicePipeline::SetVadSnrFactor(float snrFactor)
{
  mCapture->SetVadSnrFactor(snrFactor);
}

float VoicePipeline::VadSnrFactor() const
{
  return mCapture->VadSnrFactor();
}

void VoicePipeline::SetVadAttackFrames(int frames)
{
  mCapture->SetVadAttackFrames(frames);
}

int VoicePipeline::VadAttackFrames() const
{
  return mCapture->VadAttackFrames();
}

void VoicePipeline::SetVadReleaseFrames(int frames)
{
  mCapture->SetVadReleaseFrames(frames);
}

int VoicePipeline::VadReleaseFrames() const
{
  return mCapture->VadReleaseFrames();
}

void VoicePipeline::SetVadZcrRange(float minPerSecond, float maxPerSecond)
{
  mCapture->SetVadZcrRange(minPerSecond, maxPerSecond);
}

float VoicePipeline::VadZcrMin() const
{
  return mCapture->VadZcrMin();
}

float VoicePipeline::VadZcrMax() const
{
  return mCapture->VadZcrMax();
}

void VoicePipeline::SetVadMinRmsFloor(float floor)
{
  mCapture->SetVadMinRmsFloor(floor);
}

float VoicePipeline::VadMinRmsFloor() const
{
  return mCapture->VadMinRmsFloor();
}

void VoicePipeline::SetNoiseFilter(bool enabled)
{
  mCapture->SetNoiseFilter(enabled); // no-op when mWavSource is active
}

bool VoicePipeline::IsNoiseFilterEnabled() const
{
  return mCapture->IsNoiseFilterEnabled();
}

void VoicePipeline::SetInputGain(float gain)
{
  if (mWavSource)
    mWavSource->SetInputGain(gain);
  else
    mCapture->SetInputGain(gain);
}

float VoicePipeline::InputGain() const
{
  return mWavSource ? mWavSource->InputGain() : mCapture->InputGain();
}

float VoicePipeline::GetInputRms() const
{
  return mWavSource ? mWavSource->GetLastRms() : mCapture->GetLastRms();
}
