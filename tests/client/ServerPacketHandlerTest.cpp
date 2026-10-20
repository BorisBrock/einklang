// The client's decoding of server packets. Packets are built here with the
// generated FlatBuffers API; the integration tests cover the real server's
// ServerPacketFactory output.

#include <doctest.h>

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "../../src/client/Networking/ServerPacketHandler.h"
#include "../../src/common/VoicePacket.h"
#include "protocol_generated.h"

namespace
{
using Type = ServerPacketHandler::PacketType;

struct Recorder
{
  ServerPacketHandler handler;

  std::vector<std::pair<bool, std::string>> entryResponses;
  std::vector<std::pair<std::string, std::string>> chat;
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> images;
  std::vector<std::vector<std::string>> userUpdates;
  std::vector<std::vector<ChatMessage>> histories;
  std::vector<std::vector<std::uint8_t>> voice;

  Recorder()
  {
    handler.onRoomEntryResponse.subscribe([this](bool ok, std::string error)
    {
      entryResponses.emplace_back(ok, std::move(error));
    });
    handler.onNewChatMessage.subscribe([this](std::string user, std::string text)
    {
      chat.emplace_back(std::move(user), std::move(text));
    });
    handler.onNewChatImage.subscribe([this](std::string user, std::vector<std::uint8_t> data)
    {
      images.emplace_back(std::move(user), std::move(data));
    });
    handler.onRoomUsersUpdate.subscribe([this](std::vector<std::string> users)
    {
      userUpdates.push_back(std::move(users));
    });
    handler.onRoomChatHistory.subscribe([this](std::vector<ChatMessage> history)
    {
      histories.push_back(std::move(history));
    });
    handler.onVoiceFrameReceived.subscribe([this](const std::uint8_t* data, std::size_t size)
    {
      voice.emplace_back(data, data + size);
    });
  }

  void Feed(const std::vector<std::uint8_t>& packet)
  {
    handler.HandlePacket(packet.data(), packet.size());
  }

  std::size_t Total() const
  {
    return entryResponses.size() + chat.size() + images.size() + userUpdates.size() +
           histories.size() + voice.size();
  }
};

template <typename Build>
std::vector<std::uint8_t> Packet(Type type, Build build)
{
  flatbuffers::FlatBufferBuilder fbb;
  build(fbb);
  std::vector<std::uint8_t> packet{static_cast<std::uint8_t>(type)};
  packet.insert(packet.end(), fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
  return packet;
}

std::vector<std::uint8_t> EntryResponse(bool success, const char* error)
{
  return Packet(Type::RoomEntryResponse, [&](auto& fbb)
  {
    fbb.Finish(Einklang::CreateRoomEntryResponseDirect(fbb, success, error));
  });
}

std::vector<std::uint8_t> ChatPacket(const char* user, const char* text)
{
  return Packet(Type::NewChatMessage, [&](auto& fbb)
  {
    fbb.Finish(Einklang::CreateHandleNewChatMessageDirect(fbb, user, text));
  });
}
}

TEST_SUITE("ServerPacketHandler")
{
TEST_CASE("the packet type ids are part of the wire format")
{
  CHECK(static_cast<int>(Type::RoomEntryResponse) == 1);
  CHECK(static_cast<int>(Type::NewChatMessage) == 2);
  CHECK(static_cast<int>(Type::RoomUsersUpdate) == 3);
  CHECK(static_cast<int>(Type::RoomChatHistory) == 4);
  CHECK(static_cast<int>(Type::VoiceFrame) == 5);
  CHECK(static_cast<int>(Type::NewChatImage) == 6);
}

TEST_CASE("a granted room entry has no error message")
{
  Recorder r;
  r.Feed(EntryResponse(true, "ignored"));
  REQUIRE(r.entryResponses.size() == 1);
  CHECK(r.entryResponses[0].first);
  CHECK(r.entryResponses[0].second.empty());
}

TEST_CASE("a denied room entry carries the server's reason")
{
  Recorder r;
  r.Feed(EntryResponse(false, "Room is full."));
  REQUIRE(r.entryResponses.size() == 1);
  CHECK_FALSE(r.entryResponses[0].first);
  CHECK(r.entryResponses[0].second == "Room is full.");
}

TEST_CASE("an unreadable room entry response is reported as a failure")
{
  Recorder r;
  r.Feed({static_cast<std::uint8_t>(Type::RoomEntryResponse), 1, 2, 3});
  REQUIRE(r.entryResponses.size() == 1);
  CHECK_FALSE(r.entryResponses[0].first);
  CHECK_FALSE(r.entryResponses[0].second.empty());
}

TEST_CASE("new chat messages are decoded")
{
  Recorder r;
  r.Feed(ChatPacket("alice", "hi there ✨"));
  REQUIRE(r.chat.size() == 1);
  CHECK(r.chat[0].first == "alice");
  CHECK(r.chat[0].second == "hi there ✨");
}

TEST_CASE("chat messages without user or text are dropped")
{
  Recorder r;
  r.Feed(Packet(Type::NewChatMessage, [](auto& fbb)
  {
    fbb.Finish(Einklang::CreateHandleNewChatMessageDirect(fbb, nullptr, "text"));
  }));
  r.Feed(Packet(Type::NewChatMessage, [](auto& fbb)
  {
    fbb.Finish(Einklang::CreateHandleNewChatMessageDirect(fbb, "alice", nullptr));
  }));
  CHECK(r.chat.empty());
}

TEST_CASE("new chat images are decoded")
{
  Recorder r;
  const std::vector<std::uint8_t> image{0x89, 'P', 'N', 'G', 0, 1, 2};
  r.Feed(Packet(Type::NewChatImage, [&](auto& fbb)
  {
    fbb.Finish(Einklang::CreateHandleNewChatImageDirect(fbb, "bob", &image));
  }));
  REQUIRE(r.images.size() == 1);
  CHECK(r.images[0].first == "bob");
  CHECK(r.images[0].second == image);
}

TEST_CASE("chat images without data are dropped")
{
  Recorder r;
  r.Feed(Packet(Type::NewChatImage, [](auto& fbb)
  {
    fbb.Finish(Einklang::CreateHandleNewChatImageDirect(fbb, "bob", nullptr));
  }));
  CHECK(r.images.empty());
}

TEST_CASE("user list updates are decoded in order")
{
  Recorder r;
  const std::vector<std::string> users{"alice", "bob", "Çağrı"};
  r.Feed(Packet(Type::RoomUsersUpdate, [&](auto& fbb)
  {
    fbb.Finish(Einklang::CreateRoomUsersUpdate(fbb, fbb.CreateVectorOfStrings(users)));
  }));
  REQUIRE(r.userUpdates.size() == 1);
  CHECK(r.userUpdates[0] == users);
}

TEST_CASE("a user list update without users means an empty room")
{
  Recorder r;
  r.Feed(Packet(Type::RoomUsersUpdate, [](auto& fbb)
  {
    fbb.Finish(Einklang::CreateRoomUsersUpdate(fbb));
  }));
  REQUIRE(r.userUpdates.size() == 1);
  CHECK(r.userUpdates[0].empty());
}

TEST_CASE("chat history with text and image entries is decoded")
{
  Recorder r;
  const std::vector<std::uint8_t> image{1, 2, 3};
  r.Feed(Packet(Type::RoomChatHistory, [&](auto& fbb)
  {
    std::vector<flatbuffers::Offset<Einklang::ChatMessage>> entries;
    entries.push_back(Einklang::CreateChatMessageDirect(fbb, "alice", "first", 1000));
    entries.push_back(Einklang::CreateChatMessageDirect(fbb, "bob", nullptr, 2000, &image));
    entries.push_back(Einklang::CreateChatMessageDirect(fbb, nullptr, nullptr, 3000));
    fbb.Finish(Einklang::CreateRoomChatHistoryDirect(fbb, &entries));
  }));

  REQUIRE(r.histories.size() == 1);
  const auto& h = r.histories[0];
  REQUIRE(h.size() == 3);

  CHECK(h[0].kind == ChatMessage::Kind::Text);
  CHECK(h[0].userName == "alice");
  CHECK(h[0].text == "first");
  CHECK(h[0].timestampMs == 1000);
  CHECK(h[0].imageData.empty());

  CHECK(h[1].kind == ChatMessage::Kind::Image);
  CHECK(h[1].userName == "bob");
  CHECK(h[1].text.empty());
  CHECK(h[1].timestampMs == 2000);
  CHECK(h[1].imageData == image);

  // Missing fields become empty strings instead of dropping the entry
  CHECK(h[2].kind == ChatMessage::Kind::Text);
  CHECK(h[2].userName.empty());
  CHECK(h[2].text.empty());
}

TEST_CASE("an empty image vector in history counts as a text message")
{
  Recorder r;
  const std::vector<std::uint8_t> empty;
  r.Feed(Packet(Type::RoomChatHistory, [&](auto& fbb)
  {
    std::vector<flatbuffers::Offset<Einklang::ChatMessage>> entries;
    entries.push_back(Einklang::CreateChatMessageDirect(fbb, "a", "t", 1, &empty));
    fbb.Finish(Einklang::CreateRoomChatHistoryDirect(fbb, &entries));
  }));
  REQUIRE(r.histories.size() == 1);
  REQUIRE(r.histories[0].size() == 1);
  CHECK(r.histories[0][0].kind == ChatMessage::Kind::Text);
}

TEST_CASE("an empty history is delivered as an empty list")
{
  Recorder r;
  r.Feed(Packet(Type::RoomChatHistory, [](auto& fbb)
  {
    fbb.Finish(Einklang::CreateRoomChatHistory(fbb));
  }));
  REQUIRE(r.histories.size() == 1);
  CHECK(r.histories[0].empty());
}

TEST_CASE("relayed voice frames are forwarded once the minimum size is reached")
{
  Recorder r;
  std::vector<std::uint8_t> packet{static_cast<std::uint8_t>(Type::VoiceFrame)};
  packet.resize(1 + Voice::kRelayMinPayloadSize - 1, 0);
  r.Feed(packet);
  CHECK(r.voice.empty());

  packet.push_back(0x42);
  r.Feed(packet);
  REQUIRE(r.voice.size() == 1);
  CHECK(r.voice[0].size() == Voice::kRelayMinPayloadSize);
  CHECK(r.voice[0].back() == 0x42);
}

TEST_CASE("invalid input is ignored")
{
  Recorder r;

  SUBCASE("no data")
  {
    r.handler.HandlePacket(nullptr, 0);
    r.Feed({});
  }
  SUBCASE("unknown packet types")
  {
    auto packet = ChatPacket("a", "b");
    for (const std::uint8_t type : {0, 7, 100, 255})
    {
      packet[0] = type;
      r.Feed(packet);
    }
  }

  CHECK(r.Total() == 0);
}

TEST_CASE("truncated packets are rejected or decode to the original content")
{
  // Cutting off only trailing alignment padding leaves a valid buffer, so
  // the requirement is: never corrupted data.
  Recorder r;
  const auto packet = ChatPacket("alice", "a message that will be cut");
  for (std::size_t len = 1; len < packet.size(); ++len)
    r.Feed({packet.begin(), packet.begin() + static_cast<std::ptrdiff_t>(len)});

  for (const auto& [user, text] : r.chat)
  {
    CHECK(user == "alice");
    CHECK(text == "a message that will be cut");
  }
  CHECK(r.chat.size() < packet.size() / 2); // most cuts are rejected
}

TEST_CASE("random garbage never crashes the handler")
{
  Recorder r;
  std::mt19937 rng(7);
  std::uniform_int_distribution<int> byte(0, 255);
  std::uniform_int_distribution<int> length(0, 200);
  for (int i = 0; i < 20000; ++i)
  {
    std::vector<std::uint8_t> packet(static_cast<std::size_t>(length(rng)));
    for (auto& b : packet)
      b = static_cast<std::uint8_t>(byte(rng));
    if (!packet.empty())
      packet[0] = static_cast<std::uint8_t>(1 + i % 6);
    r.Feed(packet);
  }
  // Every unreadable entry response must still produce an answer for the GUI
  CHECK(r.entryResponses.size() >= 1);
}
}
