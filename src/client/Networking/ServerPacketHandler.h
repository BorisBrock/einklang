#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../../common/Event.h"
#include "../../common/ChatMessage.h"

class ServerPacketHandler
{
public:
  enum class PacketType : std::uint8_t
  {
    RoomEntryResponse = 1,
    NewChatMessage    = 2,
    RoomUsersUpdate   = 3,
    RoomChatHistory   = 4,
    VoiceFrame        = 5,
    NewChatImage      = 6
  };

  // Fired when the server responds to a room entry request
  Event<bool, std::string> onRoomEntryResponse;

  // Fired when the server broadcasts a new chat message
  // Args: userName, text
  Event<std::string, std::string> onNewChatMessage;

  // Fired when the server broadcasts a new chat image
  // Args: userName, raw image bytes
  Event<std::string, std::vector<std::uint8_t>> onNewChatImage;

  // Fired when the server sends an updated user list for the current room
  // Args: list of user names
  Event<std::vector<std::string>> onRoomUsersUpdate;

  // Fired when the server delivers the room's recent chat history upon entry
  // Args: list of history entries
  Event<std::vector<ChatMessage>> onRoomChatHistory;

  // Fired when the server relays a voice frame from another user
  // Args: raw voice data (header + opus payload), size
  Event<const std::uint8_t*, std::size_t> onVoiceFrameReceived;

  void HandlePacket(const std::uint8_t* data, std::size_t size);

private:
  void HandleRoomEntryResponse(const std::uint8_t* payloadData, std::size_t payloadSize);
  void HandleNewChatMessage(const std::uint8_t* payloadData, std::size_t payloadSize);
  void HandleNewChatImage(const std::uint8_t* payloadData, std::size_t payloadSize);
  void HandleRoomUsersUpdate(const std::uint8_t* payloadData, std::size_t payloadSize);
  void HandleRoomChatHistory(const std::uint8_t* payloadData, std::size_t payloadSize);
};
