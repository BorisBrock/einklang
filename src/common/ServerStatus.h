#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Status of a server as reported by a connectionless status query
// (see ServerQuery.h).

struct RoomStatus
{
  std::string   name;
  std::uint16_t userCount{0};
  std::uint16_t maxUsers{0};

  bool operator==(const RoomStatus&) const = default;
};

struct ServerStatus
{
  bool                    online{false};
  std::string             serverVersion;
  std::vector<RoomStatus> rooms;

  bool operator==(const ServerStatus&) const = default;
};
