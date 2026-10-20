#pragma once

#include <mutex>
#include <span>
#include <string>

// Writes decoded PCM audio (48 kHz, mono, 32-bit float) to a WAV file.
//
// Used when -capture <file> is specified: the decoder worker calls
// WriteSamples() with every decoded frame instead of (or as well as)
// pushing it to VoicePlayer.  Thread-safe.
class WavFileRecorder
{
public:
  WavFileRecorder();
  ~WavFileRecorder();

  WavFileRecorder(const WavFileRecorder&) = delete;
  WavFileRecorder& operator=(const WavFileRecorder&) = delete;

  // Open filePath for writing.  Creates or overwrites the file.
  // Returns false if miniaudio cannot open/create the encoder.
  bool Open(const std::string& filePath);

  // Append PCM samples.  May be called from any thread; serialised by mutex.
  void WriteSamples(std::span<const float> pcm);

  // Finalise the WAV header and close the file.  Safe to call multiple times.
  void Close();

  bool IsOpen() const;

private:
  struct Impl;
  Impl* mImpl{};
};
