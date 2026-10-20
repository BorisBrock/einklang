#pragma once

#include "ClientPacketHandler.h"

class EnetServer
{
public:
  bool Start(uint16_t port);
  bool Stop();

  void ProcessEvents();

  ClientPacketHandler& PacketHandler();

private:
  ClientPacketHandler mPacketHandler;
  ENetHost* mServer{};
};