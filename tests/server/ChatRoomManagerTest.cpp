#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../../src/server/Configuration/ServerConfig.h"
#include "../../src/server/Rooms/ChatRoomManager.h"
#include "TestUtils.h"

using TestUtils::TempDir;

namespace
{
ServerConfig TwoRooms(std::uint32_t maxHistory = 500)
{
  ServerConfig config;
  config.maxHistoryMessages = maxHistory;
  config.rooms.push_back({.name = "Lobby", .maxUsers = 10, .password = "pw"});
  config.rooms.push_back({.name = "Game Room", .maxUsers = 2, .password = ""});
  return config;
}
}

TEST_SUITE("ChatRoomManager")
{
TEST_CASE("creates one room per configured room")
{
  ChatRoomManager manager;
  manager.InitializeFromConfig(TwoRooms(42));

  REQUIRE(manager.Rooms().size() == 2);
  CHECK(manager.Rooms()[0].Name() == "Lobby");
  CHECK(manager.Rooms()[0].MaxUsers() == 10);
  CHECK(manager.Rooms()[0].Password() == "pw");
  CHECK(manager.Rooms()[0].MaxHistory() == 42);
  CHECK(manager.Rooms()[1].Name() == "Game Room");
  CHECK(manager.Rooms()[1].MaxUsers() == 2);
}

TEST_CASE("re-initializing replaces the rooms")
{
  ChatRoomManager manager;
  manager.InitializeFromConfig(TwoRooms());

  ServerConfig other;
  other.rooms.push_back({.name = "Only", .maxUsers = 1, .password = ""});
  manager.InitializeFromConfig(other);

  REQUIRE(manager.Rooms().size() == 1);
  CHECK(manager.FindRoom("Lobby") == nullptr);
  CHECK(manager.FindRoom("Only") != nullptr);
}

TEST_CASE("finds rooms by exact name")
{
  ChatRoomManager manager;
  manager.InitializeFromConfig(TwoRooms());
  REQUIRE(manager.FindRoom("Lobby") != nullptr);
  CHECK(manager.FindRoom("Lobby")->Name() == "Lobby");
  CHECK(manager.FindRoom("lobby") == nullptr);
  CHECK(manager.FindRoom("") == nullptr);
}

TEST_CASE("messages without a database are kept in memory")
{
  ChatRoomManager manager;
  manager.InitializeFromConfig(TwoRooms());
  manager.AddMessage("Lobby", "alice", "hi");
  manager.AddImageMessage("Lobby", "alice", {1, 2});
  manager.AddMessage("Nowhere", "alice", "ignored");

  const auto& history = manager.FindRoom("Lobby")->GetHistory();
  REQUIRE(history.size() == 2);
  CHECK(history[0].text == "hi");
  CHECK(history[0].timestampMs > 0);
  CHECK(history[1].imageData == std::vector<std::uint8_t>{1, 2});
  CHECK(manager.FindRoom("Game Room")->GetHistory().empty());
}

TEST_CASE("history is persisted and restored on the next start")
{
  TempDir dir;
  const auto dbPath = dir.File("einklang.db");

  {
    ChatRoomManager manager;
    manager.InitializeFromConfig(TwoRooms());
    manager.OpenDatabase(dbPath);
    manager.AddMessage("Lobby", "alice", "one");
    manager.AddImageMessage("Lobby", "bob", {5, 6, 7});
    manager.AddMessage("Game Room", "carol", "gg");
  }

  ChatRoomManager restarted;
  restarted.InitializeFromConfig(TwoRooms());
  restarted.OpenDatabase(dbPath);

  const auto& lobby = restarted.FindRoom("Lobby")->GetHistory();
  REQUIRE(lobby.size() == 2);
  CHECK(lobby[0].userName == "alice");
  CHECK(lobby[0].text == "one");
  CHECK(lobby[1].kind == ChatMessage::Kind::Image);
  CHECK(lobby[1].imageData == std::vector<std::uint8_t>{5, 6, 7});

  const auto& game = restarted.FindRoom("Game Room")->GetHistory();
  REQUIRE(game.size() == 1);
  CHECK(game[0].text == "gg");
}

TEST_CASE("only the last maxHistoryMessages are restored")
{
  TempDir dir;
  const auto dbPath = dir.File("einklang.db");

  {
    ChatRoomManager manager;
    manager.InitializeFromConfig(TwoRooms(100));
    manager.OpenDatabase(dbPath);
    for (int i = 0; i < 20; ++i)
      manager.AddMessage("Lobby", "u", std::to_string(i));
  }

  ChatRoomManager restarted;
  restarted.InitializeFromConfig(TwoRooms(5));
  restarted.OpenDatabase(dbPath);

  const auto& history = restarted.FindRoom("Lobby")->GetHistory();
  REQUIRE(history.size() == 5);
  CHECK(history.front().text == "15");
  CHECK(history.back().text == "19");
}

TEST_CASE("a room added to the config starts with an empty history")
{
  TempDir dir;
  const auto dbPath = dir.File("einklang.db");
  {
    ChatRoomManager manager;
    manager.InitializeFromConfig(TwoRooms());
    manager.OpenDatabase(dbPath);
    manager.AddMessage("Lobby", "u", "old");
  }

  auto config = TwoRooms();
  config.rooms.push_back({.name = "New Room", .maxUsers = 3, .password = ""});
  ChatRoomManager restarted;
  restarted.InitializeFromConfig(config);
  restarted.OpenDatabase(dbPath);

  CHECK(restarted.FindRoom("Lobby")->GetHistory().size() == 1);
  CHECK(restarted.FindRoom("New Room")->GetHistory().empty());

  restarted.AddMessage("New Room", "u", "first");
  CHECK(restarted.FindRoom("New Room")->GetHistory().size() == 1);
}

TEST_CASE("an unusable database path still leaves a working in-memory server")
{
  TempDir dir;
  ChatRoomManager manager;
  manager.InitializeFromConfig(TwoRooms());
  manager.OpenDatabase(dir.File("missing/dir/einklang.db"));

  manager.AddMessage("Lobby", "alice", "still works");
  CHECK(manager.FindRoom("Lobby")->GetHistory().size() == 1);
}
}
