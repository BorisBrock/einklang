#include "ClientPacketHandler.h"

#include <print>

#include <flatbuffers/verifier.h>
#include <flatbuffers/flatbuffers.h>

#include "../../common/VoicePacket.h"
#include "protocol_generated.h"

void ClientPacketHandler::SetLimits(std::size_t maxMessageLength, std::size_t maxUserNameLength)
{
  mMaxMessageLength = maxMessageLength;
  mMaxUserNameLength = maxUserNameLength;
}

void ClientPacketHandler::HandlePacket(ENetHost* server,
                                       ENetPeer* peer,
                                       const std::uint8_t* data,
                                       std::size_t size)
{
  if (server == nullptr || peer == nullptr || data == nullptr || size < sizeof(PacketType))
  {
    return;
  }

  const auto packetType = static_cast<PacketType>(data[0]);
  const std::uint8_t* payloadData = data + sizeof(PacketType);
  const std::size_t payloadSize = size - sizeof(PacketType);

  switch (packetType)
  {
  case PacketType::SendChatMessage:
    HandleSendChatMessage(server, peer, payloadData, payloadSize);
    break;

  case PacketType::RoomEntryRequest:
    HandleRoomEntryRequest(server, peer, payloadData, payloadSize);
    break;

  case PacketType::SendChatImage:
    HandleSendChatImage(server, peer, payloadData, payloadSize);
    break;

  case PacketType::VoiceFrame:
    // Voice frames bypass FlatBuffers; just forward the raw payload.
    // Minimum valid size: VoiceFrameHeader + at least 1 byte of Opus data.
    if (payloadSize > Voice::kHeaderSize)
    {
      onVoiceFrameReceived.emit(server, peer, payloadData, payloadSize);
    }
    break;
  }
}

void ClientPacketHandler::HandleSendChatMessage(ENetHost* server, ENetPeer* peer,
                                                   const std::uint8_t* payloadData, std::size_t payloadSize)
{
  flatbuffers::Verifier verifier(payloadData, payloadSize);
  if (!verifier.VerifyBuffer<Einklang::PublishChatMessage>(nullptr))
  {
    std::println("Received invalid SendChatMessage packet.");
    return;
  }

  const auto* message = flatbuffers::GetRoot<Einklang::PublishChatMessage>(payloadData);
  if (message == nullptr || message->text() == nullptr)
  {
    std::println("Received malformed SendChatMessage payload.");
    return;
  }

  if (message->text()->size() > mMaxMessageLength)
  {
    std::println("Received oversized chat message ({} bytes); rejecting.", message->text()->size());
    return;
  }

  onChatMessageReceived.emit(server, peer, message->text()->str());
}

void ClientPacketHandler::HandleRoomEntryRequest(ENetHost* server,
                                                 ENetPeer* peer,
                                                 const std::uint8_t* payloadData,
                                                 std::size_t payloadSize)
{
  flatbuffers::Verifier verifier(payloadData, payloadSize);
  if (!verifier.VerifyBuffer<Einklang::RoomEntryRequest>(nullptr))
  {
    std::println("Received invalid RoomEntryRequest packet.");
    return;
  }

  const auto* request = flatbuffers::GetRoot<Einklang::RoomEntryRequest>(payloadData);
  if (request == nullptr || request->room_name() == nullptr || request->user_name() == nullptr)
  {
    std::println("Received malformed RoomEntryRequest payload.");
    return;
  }

  const std::string roomName = request->room_name()->str();
  const std::string roomPassword = request->room_password() == nullptr ? "" : request->room_password()->str();
  const std::string userName = request->user_name()->str();

  if (userName.size() > mMaxUserNameLength)
  {
    std::println("Received oversized user name ({} bytes); rejecting.", userName.size());
    return;
  }

  onRoomEntryRequested.emit(server, peer, roomName, roomPassword, userName);
}

void ClientPacketHandler::HandleSendChatImage(ENetHost* server, ENetPeer* peer,
                                               const std::uint8_t* payloadData, std::size_t payloadSize)
{
  flatbuffers::Verifier verifier(payloadData, payloadSize);
  if (!verifier.VerifyBuffer<Einklang::PublishChatImage>(nullptr))
  {
    std::println("Received invalid SendChatImage packet.");
    return;
  }

  const auto* msg = flatbuffers::GetRoot<Einklang::PublishChatImage>(payloadData);
  if (msg == nullptr || msg->image_data() == nullptr)
  {
    std::println("Received malformed SendChatImage payload.");
    return;
  }

  if (msg->image_data()->size() > mMaxImageSize)
  {
    std::println("Received oversized chat image ({} bytes); rejecting.", msg->image_data()->size());
    return;
  }

  const std::vector<std::uint8_t> imageData(
    msg->image_data()->data(),
    msg->image_data()->data() + msg->image_data()->size());

  onChatImageReceived.emit(server, peer, imageData);
}
