#include <doctest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include "../../src/client/Networking/ClientPacketFactory.h"
#include "../../src/common/VoicePacket.h"
#include "protocol_generated.h"

namespace
{
template <typename Table>
const Table* Payload(const std::vector<std::uint8_t>& packet)
{
  REQUIRE(packet.size() > 1);
  flatbuffers::Verifier verifier(packet.data() + 1, packet.size() - 1);
  REQUIRE(verifier.VerifyBuffer<Table>(nullptr));
  return flatbuffers::GetRoot<Table>(packet.data() + 1);
}

std::uint8_t TypeByte(ClientPacketFactory::PacketType type)
{
  return static_cast<std::uint8_t>(type);
}
}

TEST_SUITE("ClientPacketFactory")
{
TEST_CASE("the packet type ids are part of the wire format")
{
  CHECK(TypeByte(ClientPacketFactory::PacketType::SendChatMessage) == 1);
  CHECK(TypeByte(ClientPacketFactory::PacketType::RoomEntryRequest) == 2);
  CHECK(TypeByte(ClientPacketFactory::PacketType::VoiceFrame) == 3);
  CHECK(TypeByte(ClientPacketFactory::PacketType::SendChatImage) == 4);
}

TEST_CASE("chat message packet")
{
  const auto packet = ClientPacketFactory::PublishChatMessage("hello 🌍");
  CHECK(packet[0] == TypeByte(ClientPacketFactory::PacketType::SendChatMessage));
  const auto* msg = Payload<Einklang::PublishChatMessage>(packet);
  REQUIRE(msg->text() != nullptr);
  CHECK(msg->text()->str() == "hello 🌍");
}

TEST_CASE("room entry request packet")
{
  const auto packet = ClientPacketFactory::RoomEntryRequest("Lobby", "pw", "alice");
  CHECK(packet[0] == TypeByte(ClientPacketFactory::PacketType::RoomEntryRequest));
  const auto* req = Payload<Einklang::RoomEntryRequest>(packet);
  CHECK(req->room_name()->str() == "Lobby");
  CHECK(req->room_password()->str() == "pw");
  CHECK(req->user_name()->str() == "alice");
}

TEST_CASE("an empty password is still sent as a string")
{
  const auto packet = ClientPacketFactory::RoomEntryRequest("Lobby", "", "alice");
  const auto* req = Payload<Einklang::RoomEntryRequest>(packet);
  REQUIRE(req->room_password() != nullptr);
  CHECK(req->room_password()->size() == 0);
}

TEST_CASE("chat image packet")
{
  std::vector<std::uint8_t> image(5000);
  for (std::size_t i = 0; i < image.size(); ++i)
    image[i] = static_cast<std::uint8_t>(i ^ 0x5A);

  const auto packet = ClientPacketFactory::PublishChatImage(image);
  CHECK(packet[0] == TypeByte(ClientPacketFactory::PacketType::SendChatImage));
  const auto* msg = Payload<Einklang::PublishChatImage>(packet);
  REQUIRE(msg->image_data() != nullptr);
  CHECK(std::vector<std::uint8_t>(msg->image_data()->begin(), msg->image_data()->end()) == image);
}

TEST_CASE("voice frame packet layout")
{
  const std::vector<std::uint8_t> opus{1, 2, 3, 4, 5};
  const auto packet = ClientPacketFactory::VoiceFrame(0xA1B2C3D4, opus);

  REQUIRE(packet.size() == 1 + Voice::kHeaderSize + opus.size());
  CHECK(packet[0] == TypeByte(ClientPacketFactory::PacketType::VoiceFrame));

  std::uint32_t seq = 0;
  std::memcpy(&seq, packet.data() + 1, sizeof(seq));
  CHECK(seq == 0xA1B2C3D4);
  CHECK(std::vector<std::uint8_t>(packet.begin() + 1 + Voice::kHeaderSize, packet.end()) == opus);
}

TEST_CASE("the voice header is exactly the 4-byte sequence number")
{
  CHECK(Voice::kHeaderSize == 4);
  CHECK(Voice::kRelayPrefixSize == 5);
  CHECK(Voice::kRelayMinPayloadSize == 10);
  CHECK(Voice::kFrameSize == 960);
}
}
