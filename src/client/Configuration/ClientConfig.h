#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ClientConfig
{
      // Name shown to other users in every room. Empty until set; the client
      // fills it from the OS login on startup (see ConfigManager::GetOsUserName).
      std::string userName;

      std::string lastUsedServerAddress{"localhost"};
      std::string lastUsedRoomName{"Test Room"};
      uint16_t lastUsedServerPort{24816};

      struct KnownRoom
      {
            std::string address;
            uint16_t port;
            std::string roomName;
            std::string password;
      };

      std::vector<KnownRoom> knownRooms;

      // Size of the restored (non-maximized) window, saved on close
      static constexpr int kMinWindowWidth{640};
      static constexpr int kMinWindowHeight{480};
      int windowWidth{1280};
      int windowHeight{800};
      bool windowMaximized{false};

      // GUI color theme id (see THEMES in theme.js), e.g. "dark", "light", "nord"
      std::string theme{"dark"};

      // Voice chat settings
      bool  voiceEnabled{true};       // master toggle for voice chat
      float inputGain{1.0f};          // microphone gain (0.0 – 2.0)
      float outputVolume{1.0f};       // playback volume (0.0 – 1.0)

      // Audio devices by name; empty = system default. A device that is not
      // present at startup (e.g. unplugged headset) falls back to the default
      // but stays selected, so it is used again once it is back.
      std::string inputDevice;
      std::string outputDevice;

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
