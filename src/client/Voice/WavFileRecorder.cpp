// NOTE: Do NOT define MINIAUDIO_IMPLEMENTATION here — it is defined once in
// VoiceCapture.cpp and shared across all translation units.
#include <miniaudio.h>

#include "WavFileRecorder.h"

#include <print>

#include "../../common/VoicePacket.h"

struct WavFileRecorder::Impl
{
  ma_encoder encoder{};
  bool       open{false};
  std::mutex mtx;
};

WavFileRecorder::WavFileRecorder()
  : mImpl(new Impl)
{
}

WavFileRecorder::~WavFileRecorder()
{
  Close();
  delete mImpl;
}

bool WavFileRecorder::Open(const std::string& filePath)
{
  std::lock_guard<std::mutex> lock(mImpl->mtx);

  if (mImpl->open)
    return true; // already open

  const ma_encoder_config cfg = ma_encoder_config_init(
      ma_encoding_format_wav, ma_format_f32,
      Voice::kChannels, Voice::kSampleRate);

  if (ma_encoder_init_file(filePath.c_str(), &cfg, &mImpl->encoder) != MA_SUCCESS)
  {
    std::println("[WavRecorder] Failed to open '{}' for writing.", filePath);
    return false;
  }

  mImpl->open = true;
  std::println("[WavRecorder] Recording received audio to '{}' ({} Hz, mono).",
               filePath, Voice::kSampleRate);
  return true;
}

void WavFileRecorder::WriteSamples(std::span<const float> pcm)
{
  std::lock_guard<std::mutex> lock(mImpl->mtx);
  if (!mImpl->open)
    return;

  ma_uint64 written = 0;
  ma_encoder_write_pcm_frames(&mImpl->encoder, pcm.data(),
                              static_cast<ma_uint64>(pcm.size()), &written);
}

void WavFileRecorder::Close()
{
  std::lock_guard<std::mutex> lock(mImpl->mtx);
  if (!mImpl->open)
    return;

  ma_encoder_uninit(&mImpl->encoder);
  mImpl->open = false;
  std::println("[WavRecorder] Closed — WAV file finalised.");
}

bool WavFileRecorder::IsOpen() const
{
  return mImpl->open;
}
