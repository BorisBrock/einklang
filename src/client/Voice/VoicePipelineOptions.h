#pragma once

#include <string>

// Optional overrides that change how the voice pipeline sources and sinks audio.
// Both fields are empty by default, meaning normal mic-input / speaker-output behaviour.
struct VoicePipelineOptions
{
  // If non-empty: read audio from this file instead of the microphone.
  // Any format decoded by miniaudio (WAV/FLAC/MP3/OGG/…) is accepted and
  // automatically resampled to 48 kHz mono f32.  Loops indefinitely.
  std::string playbackFile;

  // If non-empty: write all received (decoded) audio to this WAV file instead
  // of routing it through the speaker device.
  std::string captureFile;
};
