#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ClientPacketHandler.h"

class EnetServer
{
public:
  bool Start(uint16_t port);
  bool Stop();

  void ProcessEvents();

  ClientPacketHandler& PacketHandler();

  // Supplies the FlatBuffers ServerStatus payload sent in reply to
  // connectionless status queries (see common/ServerQuery.h).
  void SetStatusProvider(std::function<std::vector<std::uint8_t>()> provider);

private:
  // ENet intercept callback; answers status queries before ENet sees them.
  static int ENET_CALLBACK InterceptRawPacket(ENetHost* host, void* event);

  // Returns 1 if the received datagram was a status query (handled), 0 otherwise.
  int HandleStatusQuery();

  // Per-source rate limit for status replies; returns false if throttled.
  bool AllowStatusReply(const ENetAddress& address);

  ClientPacketHandler mPacketHandler;
  ENetHost* mServer{};

  std::function<std::vector<std::uint8_t>()> mStatusProvider;

  // Source address bytes → time (enet_time_get) of the last status reply.
  std::unordered_map<std::string, enet_uint32> mLastStatusReply;
};
