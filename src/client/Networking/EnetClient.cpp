#include <chrono>
#include <print>
#include <stdint.h>
#include <thread>
#include <enet.h>
#include "EnetClient.h"
#include "../../common/VoicePacket.h"

bool EnetClient::Connect(const std::string& host, uint16_t port)
{
  if (enet_initialize() != 0)
  {
    std::println("An error occurred while initializing ENet.");
    return false;
  }

  mClient = {0};
  mClient = enet_host_create(NULL /* create a client host */,
                             1 /* only allow 1 outgoing connection */,
                             3 /* channels: 0 = reliable chat, 1 = reserved, 2 = unreliable voice */,
                             0 /* assume any amount of incoming bandwidth */,
                             0 /* assume any amount of outgoing bandwidth */);
  if (mClient == NULL)
  {
    std::println("An error occurred while trying to create an ENet client host.");
    return false;
  }

  ENetAddress address = {0};
  ENetEvent event = {};
  mPeer = {0};
  /* Connect to server. */
  enet_address_set_host(&address, host.c_str());
  address.port = port;
  /* Initiate the connection, allocating three channels (0, 1, 2). */
  mPeer = enet_host_connect(mClient, &address, 3, 0);
  if (mPeer == nullptr)
  {
    std::println("No available peers for initiating an ENet connection.");
    return false;
  }
  /* Wait up to 1 second for the connection attempt to succeed. */
  if (enet_host_service(mClient, &event, 1000) > 0 &&
      event.type == ENET_EVENT_TYPE_CONNECT)
  {
    std::println("Connection to {}:{} succeeded.", host, port);
    enet_peer_throttle_configure(mPeer, Voice::kThrottleInterval,
                                 Voice::kThrottleAcceleration, Voice::kThrottleDeceleration);
  }
  else
  {
    /* Either the timeout expired or a disconnect event was  */
    /* received. Reset the peer and clean up.                */
    enet_peer_reset(mPeer);
    enet_host_destroy(mClient);
    enet_deinitialize();
    mClient = nullptr;
    mPeer = nullptr;
    std::println("Connection to {}:{} failed.", host, port);
    return false;
  }

  mIsConnected = true;
  mConnectedHost = host;
  mConnectedPort = port;

  StartAsyncEventLoop();

  return true;
}

bool EnetClient::EnqueuePacket(std::span<const std::uint8_t> data, enet_uint8 channel, enet_uint32 flags)
{
  if (!mIsConnected || mClient == nullptr || mPeer == nullptr || data.empty())
    return false;

  std::lock_guard<std::mutex> lock(mOutboundMutex);
  if (mOutboundQueue.size() >= kMaxOutboundQueueSize)
  {
    // Queue full: drop the incoming packet and count it.
    ++mOutboundDrops;
    return false;
  }
  mOutboundQueue.push_back({std::vector<std::uint8_t>(data.begin(), data.end()), channel, flags});
  return true;
}

bool EnetClient::SendPacket(std::span<const std::uint8_t> data, enet_uint8 channel, enet_uint32 flags)
{
  return EnqueuePacket(data, channel, flags);
}

void EnetClient::FlushOutboundQueue()
{
  std::deque<OutboundPacket> local;
  {
    std::lock_guard<std::mutex> lock(mOutboundMutex);
    local.swap(mOutboundQueue);
  }

  for (auto& pkt : local)
  {
    ENetPacket* ep = enet_packet_create(pkt.data.data(), pkt.data.size(), pkt.flags);
    if (!ep)
      continue;
    if (enet_peer_send(mPeer, pkt.channel, ep) != 0)
      enet_packet_destroy(ep);
  }

  if (!local.empty())
    enet_host_flush(mClient);
}

void EnetClient::Disconnect()
{
  if (!mIsConnected)
    return;

  StopAsyncEventLoop();

  ENetEvent event = {};

  // Disconnect
  enet_peer_disconnect(mPeer, 0);

  uint8_t disconnected = false;
  /* Allow up to 500ms for the disconnect to succeed
   * and drop any packets received packets.
   */
  while (enet_host_service(mClient, &event, 500) > 0)
  {
    switch (event.type)
    {
    case ENET_EVENT_TYPE_NONE:
      break;
    case ENET_EVENT_TYPE_CONNECT:
      break;
    case ENET_EVENT_TYPE_RECEIVE:
      enet_packet_destroy(event.packet);
      break;
    case ENET_EVENT_TYPE_DISCONNECT:
      std::println("Disconnection succeeded.");
      disconnected = true;
      break;
    case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
      break;
    }
  }

  // Drop connection, since disconnection didn't successed
  if (!disconnected)
  {
    enet_peer_reset(mPeer);
  }

  enet_host_destroy(mClient);
  enet_deinitialize();

  mIsConnected = false;
  mConnectedHost.clear();
  mConnectedPort = 0;
}

bool EnetClient::IsConnected() const
{
  return mIsConnected;
}

bool EnetClient::IsConnectedTo(const std::string& host, uint16_t port) const
{
  return mIsConnected && mConnectedHost == host && mConnectedPort == port;
}

void EnetClient::StartAsyncEventLoop()
{
  mKeepEventLoopRunning = true;

  mEventLoopThread = std::thread([this]()
  {
    ENetEvent event;

    while (mKeepEventLoopRunning)
    {
      // 1. Drain the outbound queue first so callers see minimal send latency.
      //    All enet_peer_send / enet_host_flush calls happen exclusively on
      //    this thread — no external mutex needed around ENet API calls.
      FlushOutboundQueue();

      // 2. Wait up to 1 ms for socket activity.  Unlike a fixed sleep this
      //    returns as soon as a packet arrives, so incoming voice is never
      //    delayed by the poll interval.
      const int result = enet_host_service(mClient, &event, 1);

      if (result <= 0)
        continue;

      // 3. Dispatch incoming events.  Heavy packet parsing (e.g. FlatBuffers
      //    decode) is handled inside HandlePacket; keep this switch minimal.
      switch (event.type)
      {
      case ENET_EVENT_TYPE_RECEIVE:
        mPacketHandler.HandlePacket(event.packet->data, event.packet->dataLength);
        enet_packet_destroy(event.packet);
        break;

      case ENET_EVENT_TYPE_DISCONNECT:
        std::println("{} disconnected.", event.peer->data);
        event.peer->data = NULL;
        break;

      case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
        std::println("{} disconnected due to timeout.", event.peer->data);
        event.peer->data = NULL;
        break;

      case ENET_EVENT_TYPE_CONNECT:
        break;

      case ENET_EVENT_TYPE_NONE:
        break;
      }
    }

    // Log accumulated drops on exit so they appear in the terminal.
    const int drops = mOutboundDrops.load(std::memory_order_relaxed);
    if (drops > 0)
      std::println("[ENet] Event loop exiting — {} outbound packet(s) were dropped (queue full).", drops);
  });
}

ServerPacketHandler& EnetClient::PacketHandler()
{
  return mPacketHandler;
}

void EnetClient::StopAsyncEventLoop()
{
  mKeepEventLoopRunning = false;
  mEventLoopThread.join();
}
