#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

class ClientPacketFactory
{
public:
  enum class PacketType : std::uint8_t
  {
    SendChatMessage = 1,
    RoomEntryRequest = 2,
    VoiceFrame = 3,
    SendChatImage = 4
  };

  static std::vector<std::uint8_t> PublishChatMessage(const std::string& message);
  static std::vector<std::uint8_t> RoomEntryRequest(const std::string& roomName,
                                                    const std::string& roomPassword,
                                                    const std::string& userName);

  // Builds an image packet: [PacketType::SendChatImage][FlatBuffers PublishChatImage payload]
  static std::vector<std::uint8_t> PublishChatImage(std::span<const std::uint8_t> imageData);

  // Builds a voice frame packet: [PacketType][VoiceFrameHeader][opusPayload]
  static std::vector<std::uint8_t> VoiceFrame(std::uint32_t sequenceNumber,
                                              std::span<const std::uint8_t> opusPayload);
};
