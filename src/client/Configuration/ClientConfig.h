#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ClientConfig
{
      std::string lastUsedUserName{"User"};
      std::string lastUsedServerAddress{"localhost"};
      std::string lastUsedRoomName{"Test Room"};
      uint16_t lastUsedServerPort{49152};

      struct KnownRoom
      {
            std::string address;
            uint16_t port;
            std::string roomName;
            std::string userName;
            std::string password;
      };

      std::vector<KnownRoom> knownRooms;

      int windowWidth{900};
      int windowHeight{600};

      // Voice chat settings
      bool  voiceEnabled{true};       // master toggle for voice chat
      float inputGain{1.0f};          // microphone gain (0.0 – 2.0)
      float outputVolume{1.0f};       // playback volume (0.0 – 1.0)

      // Adaptive VAD settings.
      // The hard minimum RMS floor (the minimum energy required to ever open
      // the gate, even against a noisy noise floor). 0.005 is a good default;
      // lower = more sensitive, higher = more aggressive gating.
      float vadMinRmsFloor{0.005f};  // hard floor in true silence
      float vadSnrFactor{4.0f};      // dynamic threshold multiplier
      int   vadAttackFrames{2};      // consecutive positive frames required to open
      int   vadReleaseFrames{20};    // frames to stay open after last positive
      float vadZcrMin{500.0f};       // speech-like ZCR lower bound (/sec)
      float vadZcrMax{5000.0f};      // speech-like ZCR upper bound (/sec)

      // High-pass filter: removes low-frequency noise (fan hum, rumble, etc.)
      // by applying a first-order IIR HPF with an ~80 Hz cutoff before encoding.
      bool  noiseFilter{true};
};
