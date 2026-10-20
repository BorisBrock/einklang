#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ServerConfig
{
  uint16_t port{49152};

  // Maximum number of messages kept in memory per room (oldest are evicted)
  uint32_t maxHistoryMessages{500};
  // Maximum allowed length of a single chat message (bytes)
  uint32_t maxMessageLength{2048};
  // Maximum allowed length of a user name (bytes)
  uint32_t maxUserNameLength{64};

  // Global voice chat toggle (can be overridden per room)
  bool voiceEnabled{true};

  struct RoomConfig
  {
    std::string name;
    uint16_t maxUsers;
    std::string password;
    // Per-room voice chat toggle (defaults to the global setting)
    bool voiceEnabled{true};
  };

  std::vector<RoomConfig> rooms;
};
