// The server's decoding of client packets, fed with packets from the real
// client-side ClientPacketFactory and with malformed input.

#include <doctest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "../../src/client/Networking/ClientPacketFactory.h"
#include "../../src/common/VoicePacket.h"
#include "../../src/server/Networking/ClientPacketHandler.h"
#include "protocol_generated.h"

namespace
{
struct RoomEntry
{
  std::string room, password, user;
};

// Records everything the handler emits.
struct Recorder
{
  ClientPacketHandler handler;
  ENetHost host{};
  ENetPeer peer{};

  std::vector<std::string> chat;
  std::vector<std::vector<std::uint8_t>> images;
  std::vector<RoomEntry> entries;
  std::vector<std::vector<std::uint8_t>> voice;

  Recorder()
  {
    handler.onChatMessageReceived.subscribe([this](ENetHost* h, ENetPeer* p, std::string text)
    {
      CHECK(h == &host);
      CHECK(p == &peer);
      chat.push_back(std::move(text));
    });
    handler.onChatImageReceived.subscribe([this](ENetHost*, ENetPeer*, std::vector<std::uint8_t> data)
    {
      images.push_back(std::move(data));
    });
    handler.onRoomEntryRequested.subscribe([this](ENetHost*, ENetPeer*, std::string room,
                                                  std::string password, std::string user)
    {
      entries.push_back({room, password, user});
    });
    handler.onVoiceFrameReceived.subscribe([this](ENetHost*, ENetPeer*, const std::uint8_t* data,
                                                  std::size_t size)
    {
      voice.emplace_back(data, data + size);
    });
  }

  void Feed(const std::vector<std::uint8_t>& packet)
  {
    handler.HandlePacket(&host, &peer, packet.data(), packet.size());
  }

  std::size_t Total() const { return chat.size() + images.size() + entries.size() + voice.size(); }
};

template <typename Builder>
std::vector<std::uint8_t> Packet(ClientPacketHandler::PacketType type, Builder build)
{
  flatbuffers::FlatBufferBuilder fbb;
  build(fbb);
  std::vector<std::uint8_t> packet{static_cast<std::uint8_t>(type)};
  packet.insert(packet.end(), fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
  return packet;
}
}

TEST_SUITE("ClientPacketHandler")
{
TEST_CASE("client and server agree on the packet type ids")
{
  using C = ClientPacketFactory::PacketType;
  using S = ClientPacketHandler::PacketType;
  CHECK(static_cast<int>(C::SendChatMessage) == static_cast<int>(S::SendChatMessage));
  CHECK(static_cast<int>(C::RoomEntryRequest) == static_cast<int>(S::RoomEntryRequest));
  CHECK(static_cast<int>(C::VoiceFrame) == static_cast<int>(S::VoiceFrame));
  CHECK(static_cast<int>(C::SendChatImage) == static_cast<int>(S::SendChatImage));
}

TEST_CASE("chat messages are decoded")
{
  Recorder r;
  r.Feed(ClientPacketFactory::PublishChatMessage("Hello, Wörld! 👋"));
  REQUIRE(r.chat.size() == 1);
  CHECK(r.chat[0] == "Hello, Wörld! 👋");
}

TEST_CASE("an empty chat message is passed on")
{
  Recorder r;
  r.Feed(ClientPacketFactory::PublishChatMessage(""));
  REQUIRE(r.chat.size() == 1);
  CHECK(r.chat[0].empty());
}

TEST_CASE("chat messages longer than the limit are rejected")
{
  Recorder r;
  r.handler.SetLimits(10, 64);
  r.Feed(ClientPacketFactory::PublishChatMessage(std::string(10, 'a')));
  r.Feed(ClientPacketFactory::PublishChatMessage(std::string(11, 'a')));
  REQUIRE(r.chat.size() == 1);
  CHECK(r.chat[0].size() == 10);
}

TEST_CASE("the default chat limit is 2048 bytes")
{
  Recorder r;
  r.Feed(ClientPacketFactory::PublishChatMessage(std::string(2048, 'x')));
  r.Feed(ClientPacketFactory::PublishChatMessage(std::string(2049, 'x')));
  CHECK(r.chat.size() == 1);
}

TEST_CASE("a chat message without text is rejected")
{
  Recorder r;
  r.Feed(Packet(ClientPacketHandler::PacketType::SendChatMessage, [](auto& fbb)
  {
    fbb.Finish(Einklang::CreatePublishChatMessage(fbb));
  }));
  CHECK(r.chat.empty());
}

TEST_CASE("room entry requests are decoded")
{
  Recorder r;
  r.Feed(ClientPacketFactory::RoomEntryRequest("Game Room 1", "pä$$", "Ålice"));
  REQUIRE(r.entries.size() == 1);
  CHECK(r.entries[0].room == "Game Room 1");
  CHECK(r.entries[0].password == "pä$$");
  CHECK(r.entries[0].user == "Ålice");
}

TEST_CASE("a room entry request without password means an empty password")
{
  Recorder r;
  r.Feed(Packet(ClientPacketHandler::PacketType::RoomEntryRequest, [](auto& fbb)
  {
    const auto room = fbb.CreateString("Lobby");
    const auto user = fbb.CreateString("bob");
    fbb.Finish(Einklang::CreateRoomEntryRequest(fbb, room, 0, user));
  }));
  REQUIRE(r.entries.size() == 1);
  CHECK(r.entries[0].password.empty());
}

TEST_CASE("room entry requests without room or user name are rejected")
{
  Recorder r;
  r.Feed(Packet(ClientPacketHandler::PacketType::RoomEntryRequest, [](auto& fbb)
  {
    const auto user = fbb.CreateString("bob");
    fbb.Finish(Einklang::CreateRoomEntryRequest(fbb, 0, 0, user));
  }));
  r.Feed(Packet(ClientPacketHandler::PacketType::RoomEntryRequest, [](auto& fbb)
  {
    const auto room = fbb.CreateString("Lobby");
    fbb.Finish(Einklang::CreateRoomEntryRequest(fbb, room, 0, 0));
  }));
  CHECK(r.entries.empty());
}

TEST_CASE("user names longer than the limit are rejected")
{
  Recorder r;
  r.handler.SetLimits(2048, 5);
  r.Feed(ClientPacketFactory::RoomEntryRequest("Lobby", "", "12345"));
  r.Feed(ClientPacketFactory::RoomEntryRequest("Lobby", "", "123456"));
  REQUIRE(r.entries.size() == 1);
  CHECK(r.entries[0].user == "12345");
}

TEST_CASE("chat images are decoded")
{
  Recorder r;
  std::vector<std::uint8_t> image(1000);
  for (std::size_t i = 0; i < image.size(); ++i)
    image[i] = static_cast<std::uint8_t>(i);
  r.Feed(ClientPacketFactory::PublishChatImage(image));
  REQUIRE(r.images.size() == 1);
  CHECK(r.images[0] == image);
}

TEST_CASE("chat images larger than the limit are rejected")
{
  Recorder r;
  r.handler.SetMaxImageSize(100);
  r.Feed(ClientPacketFactory::PublishChatImage(std::vector<std::uint8_t>(100, 1)));
  r.Feed(ClientPacketFactory::PublishChatImage(std::vector<std::uint8_t>(101, 1)));
  REQUIRE(r.images.size() == 1);
  CHECK(r.images[0].size() == 100);
}

TEST_CASE("a chat image without data is rejected")
{
  Recorder r;
  r.Feed(Packet(ClientPacketHandler::PacketType::SendChatImage, [](auto& fbb)
  {
    fbb.Finish(Einklang::CreatePublishChatImage(fbb));
  }));
  CHECK(r.images.empty());
}

TEST_CASE("voice frames are forwarded without the type byte")
{
  Recorder r;
  const std::vector<std::uint8_t> opus{0xAA, 0xBB, 0xCC};
  r.Feed(ClientPacketFactory::VoiceFrame(0x01020304, opus));

  REQUIRE(r.voice.size() == 1);
  REQUIRE(r.voice[0].size() == Voice::kHeaderSize + opus.size());
  Voice::VoiceFrameHeader header{};
  std::memcpy(&header, r.voice[0].data(), sizeof(header));
  CHECK(header.sequenceNumber == 0x01020304);
  CHECK(std::vector<std::uint8_t>(r.voice[0].begin() + Voice::kHeaderSize, r.voice[0].end()) == opus);
}

TEST_CASE("voice frames without Opus data are dropped")
{
  Recorder r;
  r.Feed(ClientPacketFactory::VoiceFrame(1, {}));
  r.Feed({static_cast<std::uint8_t>(ClientPacketHandler::PacketType::VoiceFrame), 1, 2});
  CHECK(r.voice.empty());
}

TEST_CASE("invalid input is ignored")
{
  Recorder r;

  SUBCASE("no data")
  {
    r.handler.HandlePacket(&r.host, &r.peer, nullptr, 0);
    r.Feed({});
  }
  SUBCASE("null host or peer")
  {
    const auto packet = ClientPacketFactory::PublishChatMessage("hi");
    r.handler.HandlePacket(nullptr, &r.peer, packet.data(), packet.size());
    r.handler.HandlePacket(&r.host, nullptr, packet.data(), packet.size());
  }
  SUBCASE("unknown packet type")
  {
    auto packet = ClientPacketFactory::PublishChatMessage("hi");
    for (const std::uint8_t type : {0, 5, 99, 255})
    {
      packet[0] = type;
      r.Feed(packet);
    }
  }
  SUBCASE("type byte only")
  {
    for (std::uint8_t type = 1; type <= 4; ++type)
      r.Feed({type});
  }

  CHECK(r.Total() == 0);
}

TEST_CASE("truncated packets are rejected or decode to the original content")
{
  // Cutting off only trailing alignment padding leaves a valid buffer, so
  // the requirement is: never corrupted data.
  Recorder r;
  const std::string text = "a longer message to cut short";
  const std::vector<std::uint8_t> imageData(64, 7);
  const auto chat = ClientPacketFactory::PublishChatMessage(text);
  const auto entry = ClientPacketFactory::RoomEntryRequest("Lobby", "pw", "alice");
  const auto image = ClientPacketFactory::PublishChatImage(imageData);
  for (const auto* packet : {&chat, &entry, &image})
    for (std::size_t len = 1; len < packet->size(); ++len)
      r.Feed({packet->begin(), packet->begin() + static_cast<std::ptrdiff_t>(len)});

  for (const auto& t : r.chat)
    CHECK(t == text);
  for (const auto& e : r.entries)
  {
    CHECK(e.room == "Lobby");
    CHECK(e.password == "pw");
    CHECK(e.user == "alice");
  }
  for (const auto& i : r.images)
    CHECK(i == imageData);
  CHECK(r.voice.empty());
}

TEST_CASE("random garbage never crashes the handler")
{
  Recorder r;
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> byte(0, 255);
  std::uniform_int_distribution<int> length(0, 200);
  for (int i = 0; i < 20000; ++i)
  {
    std::vector<std::uint8_t> packet(static_cast<std::size_t>(length(rng)));
    for (auto& b : packet)
      b = static_cast<std::uint8_t>(byte(rng));
    if (!packet.empty())
      packet[0] = static_cast<std::uint8_t>(1 + i % 4); // exercise every decoder
    r.Feed(packet);
  }
  // Anything emitted must have passed the size limits.
  for (const auto& text : r.chat)
    CHECK(text.size() <= 2048);
}
}
