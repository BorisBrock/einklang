#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../../src/server/Rooms/ChatRoom.h"

TEST_SUITE("ChatRoom")
{
TEST_CASE("exposes its configuration")
{
  const ChatRoom room("Lobby", 4, "secret", 10);
  CHECK(room.Name() == "Lobby");
  CHECK(room.MaxUsers() == 4);
  CHECK(room.Password() == "secret");
  CHECK(room.MaxHistory() == 10);
  CHECK(room.UserCount() == 0);
  CHECK(room.GetHistory().empty());
}

TEST_CASE("checks the password exactly")
{
  const ChatRoom room("Lobby", 4, "secret");
  CHECK(room.IsPasswordCorrect("secret"));
  CHECK_FALSE(room.IsPasswordCorrect("Secret"));
  CHECK_FALSE(room.IsPasswordCorrect("secret "));
  CHECK_FALSE(room.IsPasswordCorrect(""));

  const ChatRoom open("Open", 4, "");
  CHECK(open.IsPasswordCorrect(""));
  CHECK_FALSE(open.IsPasswordCorrect("anything"));
}

TEST_CASE("tracks users and the user limit")
{
  ChatRoom room("Lobby", 2, "");
  CHECK_FALSE(room.IsUserLimitReached());

  room.AddUser(User("alice"));
  CHECK(room.HasUser("alice"));
  CHECK_FALSE(room.HasUser("Alice"));
  CHECK_FALSE(room.IsUserLimitReached());

  room.AddUser(User("bob"));
  CHECK(room.UserCount() == 2);
  CHECK(room.IsUserLimitReached());
  CHECK(room.GetUserNames() == std::vector<std::string>{"alice", "bob"});

  room.RemoveUser("alice");
  CHECK_FALSE(room.HasUser("alice"));
  CHECK(room.UserCount() == 1);
  CHECK_FALSE(room.IsUserLimitReached());

  room.RemoveUser("nobody"); // no-op
  CHECK(room.UserCount() == 1);
}

TEST_CASE("a room with maxUsers 0 is always full")
{
  const ChatRoom room("Closed", 0, "");
  CHECK(room.IsUserLimitReached());
}

TEST_CASE("only users with a peer are returned as peers")
{
  ChatRoom room("Lobby", 4, "");
  ENetPeer peer{};
  room.AddUser(User("with-peer", &peer));
  room.AddUser(User("without-peer"));
  const auto peers = room.GetUserPeers();
  REQUIRE(peers.size() == 1);
  CHECK(peers[0] == &peer);
}

TEST_CASE("history keeps text and image messages in order")
{
  ChatRoom room("Lobby", 4, "", 10);
  room.AddMessage("alice", "hello", 1);
  room.AddImageMessage("bob", {1, 2, 3}, 2);

  const auto& history = room.GetHistory();
  REQUIRE(history.size() == 2);
  CHECK(history[0].kind == ChatMessage::Kind::Text);
  CHECK(history[0].userName == "alice");
  CHECK(history[0].text == "hello");
  CHECK(history[0].timestampMs == 1);
  CHECK(history[1].kind == ChatMessage::Kind::Image);
  CHECK(history[1].userName == "bob");
  CHECK(history[1].imageData == std::vector<std::uint8_t>{1, 2, 3});
}

TEST_CASE("history evicts the oldest messages beyond maxHistory")
{
  ChatRoom room("Lobby", 4, "", 3);
  for (int i = 0; i < 5; ++i)
    room.AddMessage("u", std::to_string(i), i);
  room.AddImageMessage("u", {42}, 5);

  const auto& history = room.GetHistory();
  REQUIRE(history.size() == 3);
  CHECK(history[0].text == "3");
  CHECK(history[1].text == "4");
  CHECK(history[2].kind == ChatMessage::Kind::Image);
}

TEST_CASE("maxHistory 0 keeps everything")
{
  ChatRoom room("Lobby", 4, "", 0);
  for (int i = 0; i < 1000; ++i)
    room.AddMessage("u", "m", i);
  CHECK(room.GetHistory().size() == 1000);
}
}
