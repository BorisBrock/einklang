#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <enet.h>

#include "../../common/Event.h"

class ClientPacketHandler
{
public:
  enum class PacketType : std::uint8_t
  {
    SendChatMessage = 1,
    RoomEntryRequest = 2,
    VoiceFrame = 3,
    SendChatImage = 4
  };

  // Fired when a chat message is received from a client
  // Args: server, peer, text
  Event<ENetHost*, ENetPeer*, std::string> onChatMessageReceived;

  // Fired when a chat image is received from a client
  // Args: server, peer, raw image bytes
  Event<ENetHost*, ENetPeer*, std::vector<std::uint8_t>> onChatImageReceived;

  // Fired when a room entry request is received from a client
  // Args: server, peer, roomName, roomPassword, userName
  Event<ENetHost*, ENetPeer*, std::string, std::string, std::string> onRoomEntryRequested;

  // Fired when a voice frame is received from a client
  // Args: server, peer, raw voice data (header + opus payload), size
  Event<ENetHost*, ENetPeer*, const std::uint8_t*, std::size_t> onVoiceFrameReceived;

  // Fired when a client disconnects (gracefully or via timeout)
  Event<ENetPeer*> onClientDisconnected;

  void HandlePacket(ENetHost* server, ENetPeer* peer, const std::uint8_t* data, std::size_t size);

  void SetLimits(std::size_t maxMessageLength, std::size_t maxUserNameLength);

  // Maximum accepted image size in bytes (default: 5 MiB)
  void SetMaxImageSize(std::size_t maxBytes) { mMaxImageSize = maxBytes; }

private:
  std::size_t mMaxMessageLength{2048};
  std::size_t mMaxUserNameLength{64};
  std::size_t mMaxImageSize{5 * 1024 * 1024}; // 5 MiB

  void HandleSendChatMessage(ENetHost* server, ENetPeer* peer,
                              const std::uint8_t* payloadData, std::size_t payloadSize);
  void HandleRoomEntryRequest(ENetHost* server,
                              ENetPeer* peer,
                              const std::uint8_t* payloadData,
                              std::size_t payloadSize);
  void HandleSendChatImage(ENetHost* server, ENetPeer* peer,
                            const std::uint8_t* payloadData, std::size_t payloadSize);
};
