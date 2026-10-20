#include <print>
#include <vector>
#include <stdint.h>
#include <enet.h>
#include "EnetServer.h"
#include "../../common/VoicePacket.h"

bool EnetServer::Start(uint16_t port)
{
  if (enet_initialize() != 0)
  {
    std::println("An error occurred while initializing ENet.");
    return false;
  }

  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = port;

  mServer = enet_host_create(&address, 32, 3 /* channels: 0 = reliable chat, 1 = reserved, 2 = unreliable voice */, 0, 0);
  if (mServer == NULL)
  {
    std::println("An error occurred while trying to create an ENet server host.");
    return false;
  }

  return true;
}

bool EnetServer::Stop()
{
  if (mServer)
  {
    enet_host_destroy(mServer);
    mServer = nullptr;
  }
  enet_deinitialize();
  return true;
}

ClientPacketHandler& EnetServer::PacketHandler()
{
  return mPacketHandler;
}

void EnetServer::ProcessEvents()
{
  ENetEvent event;

  /* Wait up to 100 milliseconds for an event. (WARNING: blocking) */
  while (enet_host_service(mServer, &event, 100) > 0)
  {
    switch (event.type)
    {
    case ENET_EVENT_TYPE_CONNECT:
      std::println("A new client connected on port {}.", event.peer->address.port);
      enet_peer_throttle_configure(event.peer, Voice::kThrottleInterval,
                                   Voice::kThrottleAcceleration, Voice::kThrottleDeceleration);
      break;

    case ENET_EVENT_TYPE_RECEIVE:
      mPacketHandler.HandlePacket(mServer, event.peer, event.packet->data, event.packet->dataLength);
      /* Clean up the packet now that we're done using it. */
      enet_packet_destroy(event.packet);
      break;

    case ENET_EVENT_TYPE_DISCONNECT:
      mPacketHandler.onClientDisconnected.emit(event.peer);
      event.peer->data = NULL;
      break;

    case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
      mPacketHandler.onClientDisconnected.emit(event.peer);
      event.peer->data = NULL;
      break;

    case ENET_EVENT_TYPE_NONE:
      break;
    }
  }
}