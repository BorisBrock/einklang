#pragma once

#include "../../common/Event.h"
#include "ServerPacketHandler.h"
#include <enet.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include <atomic>
#include <thread>

class EnetClient
{
public:
  bool Connect(const std::string& host, uint16_t port);

  // Enqueue a packet for sending on the ENet event-loop thread.
  // Safe to call from any thread; returns false if not connected.
  bool EnqueuePacket(std::span<const std::uint8_t> data, enet_uint8 channel = 0, enet_uint32 flags = ENET_PACKET_FLAG_RELIABLE);

  // Backwards-compatible alias — forwards to EnqueuePacket.
  bool SendPacket(std::span<const std::uint8_t> data, enet_uint8 channel = 0, enet_uint32 flags = ENET_PACKET_FLAG_RELIABLE);

  void Disconnect();

  bool IsConnected() const;
  bool IsConnectedTo(const std::string& host, uint16_t port) const;

  ServerPacketHandler& PacketHandler();

private:
  void StartAsyncEventLoop();
  void StopAsyncEventLoop();

  // Drain mOutboundQueue and call enet_peer_send for each entry.
  // Must only be called from the event-loop thread.
  void FlushOutboundQueue();

private:
  // Outbound packet queued by any thread, drained by the ENet event-loop thread.
  // Using a bounded deque: if the queue exceeds kMaxOutboundQueueSize, new
  // packets are dropped (oldest kept, newest discarded — voice quality degrades
  // gracefully rather than consuming unbounded memory).
  struct OutboundPacket
  {
    std::vector<std::uint8_t> data;
    enet_uint8  channel{0};
    enet_uint32 flags{ENET_PACKET_FLAG_RELIABLE};
  };
  static constexpr std::size_t kMaxOutboundQueueSize = 256;

  std::mutex                 mOutboundMutex;
  std::deque<OutboundPacket> mOutboundQueue;
  std::atomic<int>           mOutboundDrops{0};

  ServerPacketHandler mPacketHandler;
  std::atomic<bool>   mKeepEventLoopRunning{false};
  std::atomic<bool>   mIsConnected{false}; // read by the voice encoder thread
  std::string         mConnectedHost;
  uint16_t            mConnectedPort{0};
  std::thread         mEventLoopThread;
  ENetHost           *mClient{};
  ENetPeer           *mPeer{};
};