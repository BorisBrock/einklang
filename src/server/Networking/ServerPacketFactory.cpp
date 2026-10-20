#include "ServerPacketFactory.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <print>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "../../common/ServerQuery.h"
#include "../../common/Version.h"
#include "../../common/VoicePacket.h"
#include "protocol_generated.h"
#include "PeerSessionData.h"

namespace
{
enum class ServerPacketType : std::uint8_t
{
  RoomEntryResponse = 1,
  NewChatMessage    = 2,
  RoomUsersUpdate   = 3,
  RoomChatHistory   = 4,
  VoiceFrame        = 5,
  NewChatImage      = 6
};
}

void ServerPacketFactory::SendRoomEntryResponse(ENetHost* server, ENetPeer* peer, bool success, const std::string& errorMessage)
{
  flatbuffers::FlatBufferBuilder builder;
  const auto payload = Einklang::CreateRoomEntryResponseDirect(builder, success, errorMessage.c_str());
  builder.Finish(payload);

  const auto* payloadPtr = builder.GetBufferPointer();
  const auto payloadSize = builder.GetSize();

  std::vector<std::uint8_t> packet;
  packet.resize(sizeof(ServerPacketType) + payloadSize);
  packet[0] = static_cast<std::uint8_t>(ServerPacketType::RoomEntryResponse);
  std::memcpy(packet.data() + sizeof(ServerPacketType), payloadPtr, payloadSize);

  ENetPacket* enetPacket = enet_packet_create(packet.data(), packet.size(), ENET_PACKET_FLAG_RELIABLE);
  if (enetPacket == nullptr)
  {
    std::println("Failed to create ENet packet for RoomEntryResponse.");
    return;
  }

  if (enet_peer_send(peer, 0, enetPacket) != 0)
  {
    enet_packet_destroy(enetPacket);
    std::println("Failed to send RoomEntryResponse packet.");
    return;
  }

  enet_host_flush(server);
}

void ServerPacketFactory::BroadcastNewChatMessage(ENetHost* server,
                                                  const std::string& roomName,
                                                  const std::string& userName,
                                                  const std::string& text)
{
  flatbuffers::FlatBufferBuilder builder;
  const auto payload = Einklang::CreateHandleNewChatMessageDirect(builder,
                                                                 userName.c_str(),
                                                                 text.c_str());
  builder.Finish(payload);

  const auto* payloadPtr = builder.GetBufferPointer();
  const auto  payloadSize = builder.GetSize();

  std::vector<std::uint8_t> packetData;
  packetData.resize(sizeof(ServerPacketType) + payloadSize);
  packetData[0] = static_cast<std::uint8_t>(ServerPacketType::NewChatMessage);
  std::memcpy(packetData.data() + sizeof(ServerPacketType), payloadPtr, payloadSize);

  for (ENetPeer* peer = server->peers; peer < server->peers + server->peerCount; ++peer)
  {
    if (peer->state != ENET_PEER_STATE_CONNECTED)
      continue;
    if (peer->data == nullptr)
      continue;
    const auto* session = static_cast<const PeerSessionData*>(peer->data);
    if (session->roomName != roomName)
      continue;

    ENetPacket* enetPacket = enet_packet_create(packetData.data(), packetData.size(), ENET_PACKET_FLAG_RELIABLE);
    if (enetPacket == nullptr)
    {
      std::println("Failed to create ENet packet for NewChatMessage.");
      continue;
    }
    if (enet_peer_send(peer, 0, enetPacket) != 0)
    {
      enet_packet_destroy(enetPacket);
      std::println("Failed to send NewChatMessage to peer.");
    }
  }

  enet_host_flush(server);
}

void ServerPacketFactory::BroadcastNewChatImage(ENetHost* server,
                                                const std::string& roomName,
                                                const std::string& userName,
                                                const std::vector<std::uint8_t>& imageData)
{
  flatbuffers::FlatBufferBuilder builder;
  const auto imageVec = builder.CreateVector(imageData);
  const auto payload  = Einklang::CreateHandleNewChatImage(builder,
                                                          builder.CreateString(userName),
                                                          imageVec);
  builder.Finish(payload);

  const auto* payloadPtr  = builder.GetBufferPointer();
  const auto  payloadSize = builder.GetSize();

  std::vector<std::uint8_t> packetData;
  packetData.resize(sizeof(ServerPacketType) + payloadSize);
  packetData[0] = static_cast<std::uint8_t>(ServerPacketType::NewChatImage);
  std::memcpy(packetData.data() + sizeof(ServerPacketType), payloadPtr, payloadSize);

  for (ENetPeer* peer = server->peers; peer < server->peers + server->peerCount; ++peer)
  {
    if (peer->state != ENET_PEER_STATE_CONNECTED)
      continue;
    if (peer->data == nullptr)
      continue;
    const auto* session = static_cast<const PeerSessionData*>(peer->data);
    if (session->roomName != roomName)
      continue;

    ENetPacket* enetPacket = enet_packet_create(packetData.data(), packetData.size(), ENET_PACKET_FLAG_RELIABLE);
    if (enetPacket == nullptr)
    {
      std::println("Failed to create ENet packet for NewChatImage.");
      continue;
    }
    if (enet_peer_send(peer, 0, enetPacket) != 0)
    {
      enet_packet_destroy(enetPacket);
      std::println("Failed to send NewChatImage to peer.");
    }
  }

  enet_host_flush(server);
}

// ---------------------------------------------------------------------------
// RoomUsersUpdate
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> ServerPacketFactory::BuildRoomUsersUpdatePacket(const std::vector<std::string>& userNames)
{
  flatbuffers::FlatBufferBuilder builder;

  std::vector<flatbuffers::Offset<flatbuffers::String>> offsets;
  offsets.reserve(userNames.size());
  for (const auto& name : userNames)
    offsets.push_back(builder.CreateString(name));

  const auto usersVec = builder.CreateVector(offsets);
  const auto payload  = Einklang::CreateRoomUsersUpdate(builder, usersVec);
  builder.Finish(payload);

  const auto* payloadPtr  = builder.GetBufferPointer();
  const auto  payloadSize = builder.GetSize();

  std::vector<std::uint8_t> packetData;
  packetData.resize(sizeof(ServerPacketType) + payloadSize);
  packetData[0] = static_cast<std::uint8_t>(ServerPacketType::RoomUsersUpdate);
  std::memcpy(packetData.data() + sizeof(ServerPacketType), payloadPtr, payloadSize);
  return packetData;
}

void ServerPacketFactory::SendRoomUsersUpdate(ENetHost* server,
                                              ENetPeer* peer,
                                              const std::vector<std::string>& userNames)
{
  const auto packetData = BuildRoomUsersUpdatePacket(userNames);

  ENetPacket* enetPacket = enet_packet_create(packetData.data(), packetData.size(), ENET_PACKET_FLAG_RELIABLE);
  if (enetPacket == nullptr)
  {
    std::println("Failed to create ENet packet for RoomUsersUpdate.");
    return;
  }
  if (enet_peer_send(peer, 0, enetPacket) != 0)
  {
    enet_packet_destroy(enetPacket);
    std::println("Failed to send RoomUsersUpdate to peer.");
  }
  enet_host_flush(server);
}

void ServerPacketFactory::BroadcastRoomUsersUpdate(ENetHost* server,
                                                   const std::string& roomName,
                                                   const std::vector<std::string>& userNames)
{
  const auto packetData = BuildRoomUsersUpdatePacket(userNames);

  for (ENetPeer* peer = server->peers; peer < server->peers + server->peerCount; ++peer)
  {
    if (peer->state != ENET_PEER_STATE_CONNECTED)
      continue;
    if (peer->data == nullptr)
      continue;
    const auto* session = static_cast<const PeerSessionData*>(peer->data);
    if (session->roomName != roomName)
      continue;

    ENetPacket* enetPacket = enet_packet_create(packetData.data(), packetData.size(), ENET_PACKET_FLAG_RELIABLE);
    if (enetPacket == nullptr)
    {
      std::println("Failed to create ENet packet for RoomUsersUpdate broadcast.");
      continue;
    }
    if (enet_peer_send(peer, 0, enetPacket) != 0)
    {
      enet_packet_destroy(enetPacket);
      std::println("Failed to broadcast RoomUsersUpdate to peer.");
    }
  }

  enet_host_flush(server);
}

void ServerPacketFactory::SendRoomChatHistory(ENetHost* server,
                                              ENetPeer* peer,
                                              const std::vector<ChatMessage>& history,
                                              std::size_t maxMessages)
{
  flatbuffers::FlatBufferBuilder builder;

  const std::size_t start = history.size() > maxMessages ? history.size() - maxMessages : 0;

  std::vector<flatbuffers::Offset<Einklang::ChatMessage>> entries;
  entries.reserve(history.size() - start);
  for (std::size_t i = start; i < history.size(); ++i)
  {
    const auto& msg = history[i];
    const auto tsMs = msg.timestampMs;
    if (msg.kind == ChatMessage::Kind::Image)
    {
      const auto imageVec = builder.CreateVector(msg.imageData);
      entries.push_back(Einklang::CreateChatMessage(builder,
                                                        builder.CreateString(msg.userName),
                                                        0 /* text = null */,
                                                        tsMs,
                                                        imageVec));
    }
    else
    {
      entries.push_back(Einklang::CreateChatMessageDirect(builder,
                                                              msg.userName.c_str(),
                                                              msg.text.c_str(),
                                                              tsMs,
                                                              nullptr));
    }
  }

  const auto messagesVec = builder.CreateVector(entries);
  const auto payload     = Einklang::CreateRoomChatHistory(builder, messagesVec);
  builder.Finish(payload);

  const auto* payloadPtr  = builder.GetBufferPointer();
  const auto  payloadSize = builder.GetSize();

  std::vector<std::uint8_t> packetData;
  packetData.resize(sizeof(ServerPacketType) + payloadSize);
  packetData[0] = static_cast<std::uint8_t>(ServerPacketType::RoomChatHistory);
  std::memcpy(packetData.data() + sizeof(ServerPacketType), payloadPtr, payloadSize);

  ENetPacket* enetPacket = enet_packet_create(packetData.data(), packetData.size(), ENET_PACKET_FLAG_RELIABLE);
  if (enetPacket == nullptr)
  {
    std::println("Failed to create ENet packet for RoomChatHistory.");
    return;
  }
  if (enet_peer_send(peer, 0, enetPacket) != 0)
  {
    enet_packet_destroy(enetPacket);
    std::println("Failed to send RoomChatHistory packet.");
    return;
  }
  enet_host_flush(server);
}

void ServerPacketFactory::RelayVoiceFrame(ENetHost* server,
                                          const std::vector<ENetPeer*>& destPeers,
                                          ENetPeer* senderPeer,
                                          const std::uint8_t* voicePayload,
                                          std::size_t voicePayloadSize)
{
  // Relay packet layout:
  // [ServerPacketType][senderPeerId][senderNameLen][senderName][voicePayload (seqNum + opus)]
  // ENetPeer::connectID is a unique, stable identifier assigned by ENet for the
  // lifetime of the connection — safe to use as a per-sender key on the client.
  const std::uint32_t senderPeerId = senderPeer->connectID;
  const auto* senderSession = static_cast<const PeerSessionData*>(senderPeer->data);
  const std::string senderName = senderSession != nullptr ? senderSession->userName : std::string{};

  const std::size_t senderNameSize = std::min<std::size_t>(senderName.size(), 255);
  const auto senderNameLen = static_cast<std::uint8_t>(senderNameSize);

  const std::size_t totalSize = sizeof(ServerPacketType)
                              + sizeof(senderPeerId)
                              + sizeof(senderNameLen)
                              + senderNameSize
                              + voicePayloadSize;

  std::vector<std::uint8_t> packetData(totalSize);
  std::size_t offset = 0;
  packetData[offset++] = static_cast<std::uint8_t>(ServerPacketType::VoiceFrame);
  std::memcpy(packetData.data() + offset, &senderPeerId, sizeof(senderPeerId));
  offset += sizeof(senderPeerId);
  packetData[offset++] = senderNameLen;
  if (senderNameSize > 0)
  {
    std::memcpy(packetData.data() + offset, senderName.data(), senderNameSize);
    offset += senderNameSize;
  }
  std::memcpy(packetData.data() + offset, voicePayload, voicePayloadSize);

  for (ENetPeer* dest : destPeers)
  {
    // Don't echo back to the sender
    if (dest == senderPeer)
      continue;

    if (dest->state != ENET_PEER_STATE_CONNECTED)
      continue;

    // Voice is sent unreliably on the dedicated voice channel
    ENetPacket* enetPacket = enet_packet_create(packetData.data(), packetData.size(), 0);
    if (enetPacket == nullptr)
      continue;

    if (enet_peer_send(dest, Voice::kVoiceChannel, enetPacket) != 0)
    {
      enet_packet_destroy(enetPacket);
    }
  }

  enet_host_flush(server);
}

std::vector<std::uint8_t> ServerPacketFactory::BuildServerStatus(const std::vector<ChatRoom>& rooms)
{
  constexpr std::size_t kMaxPayloadSize = ServerQuery::kMaxReplySize - ServerQuery::kReplyHeaderSize;

  for (std::size_t roomCount = rooms.size();; --roomCount)
  {
    flatbuffers::FlatBufferBuilder builder;

    std::vector<flatbuffers::Offset<Einklang::RoomStatus>> entries;
    entries.reserve(roomCount);
    for (std::size_t i = 0; i < roomCount; ++i)
    {
      const auto& room = rooms[i];
      entries.push_back(Einklang::CreateRoomStatusDirect(
        builder,
        room.Name().c_str(),
        static_cast<std::uint16_t>(std::min<std::size_t>(room.UserCount(), UINT16_MAX)),
        room.MaxUsers()));
    }

    const auto payload = Einklang::CreateServerStatusDirect(builder, EINKLANG_VERSION_STRING, &entries);
    builder.Finish(payload);

    if (builder.GetSize() <= kMaxPayloadSize || roomCount == 0)
      return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
  }
}
