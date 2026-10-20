#include "ServerPacketHandler.h"

#include <flatbuffers/verifier.h>
#include <flatbuffers/flatbuffers.h>
#include <print>
#include <string>

#include "../../common/VoicePacket.h"
#include "protocol_generated.h"

void ServerPacketHandler::HandlePacket(const std::uint8_t* data, std::size_t size)
{
  if (data == nullptr || size < sizeof(PacketType))
  {
    return;
  }

  const auto packetType = static_cast<PacketType>(data[0]);
  const std::uint8_t* payloadData = data + sizeof(PacketType);
  const std::size_t payloadSize = size - sizeof(PacketType);

  switch (packetType)
  {
  case PacketType::RoomEntryResponse:
    HandleRoomEntryResponse(payloadData, payloadSize);
    break;

  case PacketType::NewChatMessage:
    HandleNewChatMessage(payloadData, payloadSize);
    break;

  case PacketType::NewChatImage:
    HandleNewChatImage(payloadData, payloadSize);
    break;

  case PacketType::RoomUsersUpdate:
    HandleRoomUsersUpdate(payloadData, payloadSize);
    break;

  case PacketType::RoomChatHistory:
    HandleRoomChatHistory(payloadData, payloadSize);
    break;

  case PacketType::VoiceFrame:
    // Voice frames bypass FlatBuffers; forward raw data to the voice pipeline.
    // The relay metadata prefix and at least 1 byte of Opus data must be present.
    if (payloadSize >= Voice::kRelayMinPayloadSize)
    {
      onVoiceFrameReceived.emit(payloadData, payloadSize);
    }
    break;
  }
}

void ServerPacketHandler::HandleRoomEntryResponse(const std::uint8_t* payloadData, std::size_t payloadSize)
{
  flatbuffers::Verifier verifier(payloadData, payloadSize);
  if (!verifier.VerifyBuffer<Einklang::RoomEntryResponse>(nullptr))
  {
    std::println("Received invalid RoomEntryResponse packet.");
    onRoomEntryResponse.emit(false, "Invalid response from server.");
    return;
  }

  const auto* response = flatbuffers::GetRoot<Einklang::RoomEntryResponse>(payloadData);
  if (response == nullptr)
  {
    std::println("Received malformed RoomEntryResponse payload.");
    onRoomEntryResponse.emit(false, "Malformed response from server.");
    return;
  }

  const bool success = response->success();
  const std::string errorMessage = (!success && response->error_message() != nullptr)
    ? response->error_message()->str()
    : "";

  onRoomEntryResponse.emit(success, errorMessage);
}

void ServerPacketHandler::HandleNewChatMessage(const std::uint8_t* payloadData, std::size_t payloadSize)
{
  flatbuffers::Verifier verifier(payloadData, payloadSize);
  if (!verifier.VerifyBuffer<Einklang::HandleNewChatMessage>(nullptr))
  {
    std::println("Received invalid NewChatMessage packet.");
    return;
  }

  const auto* msg = flatbuffers::GetRoot<Einklang::HandleNewChatMessage>(payloadData);
  if (msg == nullptr || msg->user() == nullptr || msg->text() == nullptr)
  {
    std::println("Received malformed NewChatMessage payload.");
    return;
  }

  onNewChatMessage.emit(msg->user()->str(), msg->text()->str());
}

void ServerPacketHandler::HandleNewChatImage(const std::uint8_t* payloadData, std::size_t payloadSize)
{
  flatbuffers::Verifier verifier(payloadData, payloadSize);
  if (!verifier.VerifyBuffer<Einklang::HandleNewChatImage>(nullptr))
  {
    std::println("Received invalid NewChatImage packet.");
    return;
  }

  const auto* msg = flatbuffers::GetRoot<Einklang::HandleNewChatImage>(payloadData);
  if (msg == nullptr || msg->user() == nullptr || msg->image_data() == nullptr)
  {
    std::println("Received malformed NewChatImage payload.");
    return;
  }

  const std::vector<std::uint8_t> imageData(
    msg->image_data()->data(),
    msg->image_data()->data() + msg->image_data()->size());

  onNewChatImage.emit(msg->user()->str(), imageData);
}

void ServerPacketHandler::HandleRoomUsersUpdate(const std::uint8_t* payloadData, std::size_t payloadSize)
{
  flatbuffers::Verifier verifier(payloadData, payloadSize);
  if (!verifier.VerifyBuffer<Einklang::RoomUsersUpdate>(nullptr))
  {
    std::println("Received invalid RoomUsersUpdate packet.");
    return;
  }

  const auto* update = flatbuffers::GetRoot<Einklang::RoomUsersUpdate>(payloadData);
  if (update == nullptr)
  {
    std::println("Received malformed RoomUsersUpdate payload.");
    return;
  }

  std::vector<std::string> userNames;
  if (update->users() != nullptr)
  {
    userNames.reserve(update->users()->size());
    for (const auto* name : *update->users())
    {
      if (name != nullptr)
        userNames.push_back(name->str());
    }
  }

  onRoomUsersUpdate.emit(std::move(userNames));
}

void ServerPacketHandler::HandleRoomChatHistory(const std::uint8_t* payloadData, std::size_t payloadSize)
{
  flatbuffers::Verifier verifier(payloadData, payloadSize);
  if (!verifier.VerifyBuffer<Einklang::RoomChatHistory>(nullptr))
  {
    std::println("Received invalid RoomChatHistory packet.");
    return;
  }

  const auto* payload = flatbuffers::GetRoot<Einklang::RoomChatHistory>(payloadData);
  if (payload == nullptr)
  {
    std::println("Received malformed RoomChatHistory payload.");
    return;
  }

  std::vector<ChatMessage> entries;
  if (payload->messages() != nullptr)
  {
    entries.reserve(payload->messages()->size());
    for (const auto* entry : *payload->messages())
    {
      if (entry == nullptr) continue;
    entries.push_back(ChatMessage{
        .userName    = entry->user() != nullptr ? entry->user()->str() : "",
        .text        = entry->text()   != nullptr ? entry->text()->str()   : "",
        .timestampMs = entry->timestamp_ms(),
        .kind        = (entry->image_data() != nullptr && entry->image_data()->size() > 0)
                         ? ChatMessage::Kind::Image
                         : ChatMessage::Kind::Text,
        .imageData   = (entry->image_data() != nullptr && entry->image_data()->size() > 0)
                         ? std::vector<std::uint8_t>(entry->image_data()->data(),
                                                     entry->image_data()->data() + entry->image_data()->size())
                         : std::vector<std::uint8_t>{}
      });
    }
  }

  onRoomChatHistory.emit(std::move(entries));
}
