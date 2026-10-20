#include <algorithm>
#include <cstring>
#include <print>
#include <vector>
#include <stdint.h>
#include <enet.h>
#include "EnetServer.h"
#include "../../common/ServerQuery.h"
#include "../../common/VoicePacket.h"

namespace
{
// ENet's intercept callback carries no user pointer; there is one server per process.
EnetServer* gInterceptTarget = nullptr;

// Minimum time between two status replies to the same source address.
constexpr enet_uint32 kStatusReplyIntervalMs = 100;

// Prune the rate-limit table once it grows beyond this many entries.
constexpr std::size_t kMaxRateLimitEntries = 4096;
}

bool EnetServer::Start(uint16_t port, std::size_t maxConnections)
{
  if (enet_initialize() != 0)
  {
    std::println("An error occurred while initializing ENet.");
    return false;
  }

  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = port;

  maxConnections = std::clamp<std::size_t>(maxConnections, 1, ENET_PROTOCOL_MAXIMUM_PEER_ID);
  std::println("Accepting up to {} simultaneous connection(s).", maxConnections);

  mServer = enet_host_create(&address, maxConnections, 3 /* channels: 0 = reliable chat, 1 = reserved, 2 = unreliable voice */, 0, 0);
  if (mServer == NULL)
  {
    std::println("An error occurred while trying to create an ENet server host.");
    return false;
  }

  gInterceptTarget = this;
  enet_host_set_intercept(mServer, &EnetServer::InterceptRawPacket);

  return true;
}

bool EnetServer::Stop()
{
  if (mServer)
  {
    // Tell connected clients right away so they start reconnecting instead
    // of waiting for a timeout.
    for (ENetPeer* peer = mServer->peers; peer < mServer->peers + mServer->peerCount; ++peer)
    {
      if (peer->state == ENET_PEER_STATE_CONNECTED)
        enet_peer_disconnect_now(peer, 0);
    }
    enet_host_flush(mServer);

    enet_host_destroy(mServer);
    mServer = nullptr;
  }
  gInterceptTarget = nullptr;
  enet_deinitialize();
  return true;
}

ClientPacketHandler& EnetServer::PacketHandler()
{
  return mPacketHandler;
}

void EnetServer::SetStatusProvider(std::function<std::vector<std::uint8_t>()> provider)
{
  mStatusProvider = std::move(provider);
}

int ENET_CALLBACK EnetServer::InterceptRawPacket(ENetHost* host, void* /*event*/)
{
  if (gInterceptTarget == nullptr || gInterceptTarget->mServer != host)
    return 0;
  return gInterceptTarget->HandleStatusQuery();
}

int EnetServer::HandleStatusQuery()
{
  std::uint32_t token = 0;
  if (!ServerQuery::ParseRequest({mServer->receivedData, mServer->receivedDataLength}, token))
    return 0; // not a status query — let ENet process it

  if (!mStatusProvider || !AllowStatusReply(mServer->receivedAddress))
    return 1;

  const auto payload = mStatusProvider();

  std::vector<std::uint8_t> reply(ServerQuery::kReplyHeaderSize + payload.size());
  std::memcpy(reply.data(), ServerQuery::kReplyMagic.data(), ServerQuery::kReplyMagic.size());
  std::memcpy(reply.data() + ServerQuery::kReplyMagic.size(), &token, sizeof(token));
  std::memcpy(reply.data() + ServerQuery::kReplyHeaderSize, payload.data(), payload.size());

  ENetBuffer buffer;
  buffer.data       = reply.data();
  buffer.dataLength = reply.size();
  enet_socket_send(mServer->socket, &mServer->receivedAddress, &buffer, 1);
  return 1;
}

bool EnetServer::AllowStatusReply(const ENetAddress& address)
{
  const enet_uint32 now = enet_time_get();

  if (mLastStatusReply.size() > kMaxRateLimitEntries)
  {
    std::erase_if(mLastStatusReply, [now](const auto& entry)
    {
      return ENET_TIME_DIFFERENCE(now, entry.second) >= kStatusReplyIntervalMs;
    });
  }

  const std::string key(reinterpret_cast<const char*>(&address.host), sizeof(address.host));
  const auto [it, inserted] = mLastStatusReply.try_emplace(key, now);
  if (inserted)
    return true;

  if (ENET_TIME_DIFFERENCE(now, it->second) < kStatusReplyIntervalMs)
    return false;

  it->second = now;
  return true;
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