#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "../../common/VoicePacket.h"

struct VoiceActivityDetectorConfig
{
  float snrFactor{4.0f};
  int   attackFrames{2};
  int   releaseFrames{20};
  float zcrMin{500.0f};
  float zcrMax{5000.0f};
  float minRmsFloor{0.005f};
  std::size_t noiseWindowFrames{100};
  std::size_t preRollFrames{2};
};

class VoiceActivityDetector
{
public:
  struct FrameFeatures
  {
    float rms{0.0f};
    float ste{0.0f};
    float zcrPerSecond{0.0f};
  };

  struct ProcessResult
  {
    bool active{false};
    std::size_t outputFrameCount{0};
    FrameFeatures features{};
  };

  VoiceActivityDetector();

  // Process one 20 ms frame and emit 0..3 output frames.
  // The input should already be normalised to [-1, 1].
  ProcessResult ProcessFrame(
      std::span<const float> frame,
      const VoiceActivityDetectorConfig& config,
      std::array<std::array<float, Voice::kFrameSize>, 3>& outputFrames);

  void Reset();

private:
  FrameFeatures ComputeFeatures(std::span<const float> frame) const;
  void UpdateNoiseFloor(float rms, const VoiceActivityDetectorConfig& config);
  bool EvaluateRawDetection(const FrameFeatures& features,
                            const VoiceActivityDetectorConfig& config) const;

  // Fixed-size rings so the detector never allocates on the audio thread.
  static constexpr std::size_t kMaxNoiseWindow = 256;
  static constexpr std::size_t kMaxPreRoll     = 2;

  std::array<float, kMaxNoiseWindow> mEnergyHistory{};
  std::size_t mEnergyCount{0};
  std::size_t mEnergyHead{0};
  float mNoiseFloor{0.0f};
  bool  mHasNoiseFloor{false};

  bool mVadActive{false};
  int  mAttackCounter{0};
  int  mReleaseCounter{0};

  std::array<std::array<float, Voice::kFrameSize>, kMaxPreRoll> mPreRoll{};
  std::size_t mPreRollCount{0};
  std::size_t mPreRollHead{0}; // index of the oldest entry
};
