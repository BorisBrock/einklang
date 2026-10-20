#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <enet.h>

#include "../Rooms/ChatRoom.h"

class ServerPacketFactory
{
public:
  static void SendRoomEntryResponse(ENetHost* server, ENetPeer* peer, bool success, const std::string& errorMessage);

  // Broadcasts a HandleNewChatMessage packet to every peer in the given room.
  static void BroadcastNewChatMessage(ENetHost* server,
                                      const std::string& roomName,
                                      const std::string& userName,
                                      const std::string& text);

  // Broadcasts a HandleNewChatImage packet to every peer in the given room.
  static void BroadcastNewChatImage(ENetHost* server,
                                    const std::string& roomName,
                                    const std::string& userName,
                                    const std::vector<std::uint8_t>& imageData);

  // Sends a RoomUsersUpdate packet to a single peer.
  static void SendRoomUsersUpdate(ENetHost* server,
                                  ENetPeer* peer,
                                  const std::vector<std::string>& userNames);

  // Broadcasts a RoomUsersUpdate packet to every peer in the given room.
  static void BroadcastRoomUsersUpdate(ENetHost* server,
                                       const std::string& roomName,
                                       const std::vector<std::string>& userNames);

  // Sends the last maxMessages chat history entries to a single peer.
  static void SendRoomChatHistory(ENetHost* server,
                                  ENetPeer* peer,
                                  const std::vector<ChatMessage>& history,
                                  std::size_t maxMessages = 20);

  // Relays a voice frame from one peer to a list of destination peers.
  // The raw voice payload (VoiceFrameHeader + Opus data) is wrapped with a
  // ServerPacketType::VoiceFrame prefix and sent unreliably on channel 2.
  static void RelayVoiceFrame(ENetHost* server,
                              const std::vector<ENetPeer*>& destPeers,
                              ENetPeer* senderPeer,
                              const std::uint8_t* voicePayload,
                              std::size_t voicePayloadSize);

private:
  // Serialises a RoomUsersUpdate flatbuffer; returns the raw packet bytes
  // (type byte prepended).
  static std::vector<std::uint8_t> BuildRoomUsersUpdatePacket(const std::vector<std::string>& userNames);
};
