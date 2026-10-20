#include "ClientPacketFactory.h"

#include <flatbuffers/flatbuffers.h>
#include <cstddef>
#include <cstring>

#include "../../common/VoicePacket.h"
#include "protocol_generated.h"

std::vector<std::uint8_t> ClientPacketFactory::PublishChatMessage(const std::string& message)
{
  flatbuffers::FlatBufferBuilder builder;
  const auto payload = Einklang::CreatePublishChatMessageDirect(builder, message.c_str());
  builder.Finish(payload);

  const std::uint8_t* payloadPtr = builder.GetBufferPointer();
  const std::size_t payloadSize = builder.GetSize();

  std::vector<std::uint8_t> packet;
  packet.resize(sizeof(PacketType) + payloadSize);
  packet[0] = static_cast<std::uint8_t>(PacketType::SendChatMessage);
  std::memcpy(packet.data() + sizeof(PacketType), payloadPtr, payloadSize);

  return packet;
}

std::vector<std::uint8_t> ClientPacketFactory::RoomEntryRequest(const std::string& roomName,
                                                                const std::string& roomPassword,
                                                                const std::string& userName)
{
  flatbuffers::FlatBufferBuilder builder;
  const auto payload = Einklang::CreateRoomEntryRequestDirect(builder,
                                                            roomName.c_str(),
                                                            roomPassword.c_str(),
                                                            userName.c_str());
  builder.Finish(payload);

  const std::uint8_t* payloadPtr = builder.GetBufferPointer();
  const std::size_t payloadSize = builder.GetSize();

  std::vector<std::uint8_t> packet;
  packet.resize(sizeof(PacketType) + payloadSize);
  packet[0] = static_cast<std::uint8_t>(PacketType::RoomEntryRequest);
  std::memcpy(packet.data() + sizeof(PacketType), payloadPtr, payloadSize);

  return packet;
}

std::vector<std::uint8_t> ClientPacketFactory::PublishChatImage(std::span<const std::uint8_t> imageData)
{
  flatbuffers::FlatBufferBuilder builder;
  const auto imgVec  = builder.CreateVector(imageData.data(), imageData.size());
  const auto payload = Einklang::CreatePublishChatImage(builder, imgVec);
  builder.Finish(payload);

  const std::uint8_t* payloadPtr  = builder.GetBufferPointer();
  const std::size_t   payloadSize = builder.GetSize();

  std::vector<std::uint8_t> packet;
  packet.resize(sizeof(PacketType) + payloadSize);
  packet[0] = static_cast<std::uint8_t>(PacketType::SendChatImage);
  std::memcpy(packet.data() + sizeof(PacketType), payloadPtr, payloadSize);

  return packet;
}

std::vector<std::uint8_t> ClientPacketFactory::VoiceFrame(std::uint32_t sequenceNumber,
                                                          std::span<const std::uint8_t> opusPayload)
{
  const std::size_t totalSize = sizeof(PacketType) + Voice::kHeaderSize + opusPayload.size();

  std::vector<std::uint8_t> packet(totalSize);
  packet[0] = static_cast<std::uint8_t>(PacketType::VoiceFrame);

  Voice::VoiceFrameHeader header{};
  header.sequenceNumber = sequenceNumber;
  std::memcpy(packet.data() + sizeof(PacketType), &header, Voice::kHeaderSize);
  if (!opusPayload.empty())
    std::memcpy(packet.data() + sizeof(PacketType) + Voice::kHeaderSize,
                opusPayload.data(), opusPayload.size());

  return packet;
}
