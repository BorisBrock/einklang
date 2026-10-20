#include "VoiceActivityDetector.h"

#include <algorithm>
#include <cmath>

VoiceActivityDetector::VoiceActivityDetector() = default;

VoiceActivityDetector::ProcessResult VoiceActivityDetector::ProcessFrame(
    std::span<const float> frame,
    const VoiceActivityDetectorConfig& config,
    std::array<std::array<float, Voice::kFrameSize>, 3>& outputFrames)
{
  ProcessResult result{};
  if (frame.size() != Voice::kFrameSize)
    return result;

  const std::size_t preRollLimit = std::min(config.preRollFrames, kMaxPreRoll);

  result.features = ComputeFeatures(frame);

  if (!mVadActive)
    UpdateNoiseFloor(result.features.rms, config);

  const bool rawPositive = EvaluateRawDetection(result.features, config);
  bool justOpened = false;

  if (!mVadActive)
  {
    if (rawPositive)
      ++mAttackCounter;
    else
      mAttackCounter = 0;

    if (mAttackCounter >= std::max(1, config.attackFrames))
    {
      mVadActive = true;
      justOpened = true;
      mAttackCounter = 0;
      mReleaseCounter = std::max(0, config.releaseFrames);
    }
  }
  else
  {
    if (rawPositive)
      mReleaseCounter = std::max(0, config.releaseFrames);
    else if (mReleaseCounter > 0)
      --mReleaseCounter;
    else
      mVadActive = false;
  }

  if (mVadActive)
  {
    if (justOpened)
    {
      // Emit pre-roll first, then current frame.
      const std::size_t framesToCopy = std::min(mPreRollCount, preRollLimit);
      const std::size_t skip = mPreRollCount - framesToCopy; // keep the newest ones
      for (std::size_t i = 0; i < framesToCopy; ++i)
        outputFrames[i] = mPreRoll[(mPreRollHead + skip + i) % kMaxPreRoll];

      std::copy(frame.begin(), frame.end(), outputFrames[framesToCopy].begin());
      result.outputFrameCount = framesToCopy + 1;
    }
    else
    {
      std::copy(frame.begin(), frame.end(), outputFrames[0].begin());
      result.outputFrameCount = 1;
    }
  }

  // Keep rolling pre-roll history after processing this frame.
  if (preRollLimit == 0)
  {
    mPreRollCount = 0;
  }
  else
  {
    const std::size_t tail = (mPreRollHead + mPreRollCount) % kMaxPreRoll;
    std::copy(frame.begin(), frame.end(), mPreRoll[tail].begin());
    if (mPreRollCount < kMaxPreRoll)
      ++mPreRollCount;
    else
      mPreRollHead = (mPreRollHead + 1) % kMaxPreRoll;
  }

  result.active = mVadActive;
  return result;
}

void VoiceActivityDetector::Reset()
{
  mEnergyCount  = 0;
  mEnergyHead   = 0;
  mPreRollCount = 0;
  mPreRollHead  = 0;
  mNoiseFloor = 0.0f;
  mHasNoiseFloor = false;
  mVadActive = false;
  mAttackCounter = 0;
  mReleaseCounter = 0;
}

VoiceActivityDetector::FrameFeatures VoiceActivityDetector::ComputeFeatures(std::span<const float> frame) const
{
  FrameFeatures features{};

  float sumSq = 0.0f;
  int zeroCrossings = 0;

  // Input is guaranteed normalised to [-1, 1] by the caller (VoiceCapture).
  float prev = frame[0];
  sumSq += prev * prev;

  for (std::size_t i = 1; i < frame.size(); ++i)
  {
    const float current = frame[i];
    sumSq += current * current;

    // A zero crossing occurs whenever adjacent samples have opposite signs.
    if ((prev >= 0.0f) != (current >= 0.0f))
      ++zeroCrossings;

    prev = current;
  }

  // ZCR rate: crossings per frame × frames per second = crossings × sampleRate / N.
  const float frameSize = static_cast<float>(frame.size());
  features.ste = sumSq / frameSize;
  features.rms = std::sqrt(features.ste);
  features.zcrPerSecond = static_cast<float>(zeroCrossings) *
      static_cast<float>(Voice::kSampleRate) / frameSize;
  return features;
}

void VoiceActivityDetector::UpdateNoiseFloor(float rms, const VoiceActivityDetectorConfig& config)
{
  const std::size_t windowSize = std::clamp<std::size_t>(config.noiseWindowFrames, 1, kMaxNoiseWindow);
  if (mEnergyCount > windowSize)
    mEnergyCount = windowSize; // window shrank: forget the oldest entries
  mEnergyHistory[mEnergyHead] = rms;
  mEnergyHead = (mEnergyHead + 1) % windowSize;
  if (mEnergyCount < windowSize)
    ++mEnergyCount;

  float windowMin = rms;
  for (std::size_t i = 0; i < mEnergyCount; ++i)
    windowMin = std::min(windowMin, mEnergyHistory[i]);
  const float minFloor = std::max(0.0f, config.minRmsFloor);
  const float candidateFloor = std::max(windowMin, minFloor);

  if (!mHasNoiseFloor)
  {
    mNoiseFloor = candidateFloor;
    mHasNoiseFloor = true;
    return;
  }

  // Exponential smoothing (95/5) for stable threshold evolution.
  mNoiseFloor = 0.95f * mNoiseFloor + 0.05f * candidateFloor;
}

bool VoiceActivityDetector::EvaluateRawDetection(const FrameFeatures& features,
                                                 const VoiceActivityDetectorConfig& config) const
{
  const float floor = std::max(mNoiseFloor, std::max(0.0f, config.minRmsFloor));
  const float threshold = floor * std::max(0.1f, config.snrFactor);

  const bool primary = features.rms > threshold;
  const bool zcrInSpeechBand =
      (features.zcrPerSecond >= config.zcrMin) &&
      (features.zcrPerSecond <= config.zcrMax);
  const bool secondary = (features.rms > threshold * 0.7f) && zcrInSpeechBand;
  return primary || secondary;
}
