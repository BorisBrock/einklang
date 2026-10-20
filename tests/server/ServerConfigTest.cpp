#include <doctest.h>

#include <string>

#include "../../src/server/Configuration/ConfigManager.h"
#include "TestUtils.h"

using TestUtils::TempDir;

TEST_SUITE("ServerConfig")
{
TEST_CASE("defaults match the documented values")
{
  const ServerConfig config;
  CHECK(config.port == 24816);
  CHECK(config.maxConnections == 0);
  CHECK(config.maxHistoryMessages == 500);
  CHECK(config.maxMessageLength == 2048);
  CHECK(config.maxUserNameLength == 64);
  CHECK(config.voiceEnabled);
  CHECK(config.rooms.empty());
}

TEST_CASE("the shipped server-config.yml loads")
{
  ConfigManager manager(EINKLANG_SOURCE_DIR "/src/server/server-config.yml");
  REQUIRE(manager.Load());

  const auto& config = manager.Get();
  CHECK(config.port == 24816);
  CHECK(config.maxConnections == 0); // commented out in the template
  CHECK(config.maxHistoryMessages == 500);
  CHECK(config.maxMessageLength == 2048);
  CHECK(config.maxUserNameLength == 64);
  CHECK(config.voiceEnabled);
  REQUIRE(config.rooms.size() == 2);
  CHECK(config.rooms[0].name == "Lobby");
  CHECK(config.rooms[0].maxUsers == 10);
  CHECK(config.rooms[0].password == "lobby123");
  CHECK(config.rooms[1].name == "Game Room 1");
  CHECK(config.rooms[1].maxUsers == 4);
}

TEST_CASE("every key is read")
{
  TempDir dir;
  const auto path = dir.Write("server-config.yml", R"(
port: 4000
maxConnections: 12
maxHistoryMessages: 50
maxMessageLength: 100
maxUserNameLength: 20
voiceEnabled: false
rooms:
  - name: "Alpha"
    maxUsers: 3
    password: ""
    voiceEnabled: true
  - name: "Beta Ünïcode"
    maxUsers: 65535
    password: "p@ss: word"
)");

  ConfigManager manager(path);
  REQUIRE(manager.Load());
  const auto& config = manager.Get();

  CHECK(config.port == 4000);
  CHECK(config.maxConnections == 12);
  CHECK(config.maxHistoryMessages == 50);
  CHECK(config.maxMessageLength == 100);
  CHECK(config.maxUserNameLength == 20);
  CHECK_FALSE(config.voiceEnabled);

  REQUIRE(config.rooms.size() == 2);
  CHECK(config.rooms[0].name == "Alpha");
  CHECK(config.rooms[0].maxUsers == 3);
  CHECK(config.rooms[0].password.empty());
  CHECK(config.rooms[0].voiceEnabled);

  CHECK(config.rooms[1].name == "Beta Ünïcode");
  CHECK(config.rooms[1].maxUsers == 65535);
  CHECK(config.rooms[1].password == "p@ss: word");
}

TEST_CASE("a room without voiceEnabled inherits the global setting")
{
  TempDir dir;
  for (const bool global : {true, false})
  {
    CAPTURE(global);
    const auto path = dir.Write("server-config.yml",
      std::string("voiceEnabled: ") + (global ? "true" : "false") + R"(
rooms:
  - name: "Room"
    maxUsers: 2
    password: ""
)");
    ConfigManager manager(path);
    REQUIRE(manager.Load());
    REQUIRE(manager.Get().rooms.size() == 1);
    CHECK(manager.Get().rooms[0].voiceEnabled == global);
  }
}

TEST_CASE("omitted keys keep their defaults")
{
  TempDir dir;
  const auto path = dir.Write("server-config.yml", "port: 5000\n");

  ConfigManager manager(path);
  REQUIRE(manager.Load());
  const auto& config = manager.Get();
  CHECK(config.port == 5000);
  CHECK(config.maxHistoryMessages == 500);
  CHECK(config.maxMessageLength == 2048);
  CHECK(config.maxUserNameLength == 64);
  CHECK(config.voiceEnabled);
  CHECK(config.rooms.empty());
}

TEST_CASE("loading fails for missing or invalid files")
{
  TempDir dir;

  SUBCASE("missing file")
  {
    ConfigManager manager(dir.File("does-not-exist.yml"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("malformed YAML")
  {
    ConfigManager manager(dir.Write("bad.yml", "port: [1, 2\nrooms: {"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("non-numeric port")
  {
    ConfigManager manager(dir.Write("bad.yml", "port: abc\n"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("port out of range")
  {
    ConfigManager manager(dir.Write("bad.yml", "port: 70000\n"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("negative maxUsers")
  {
    ConfigManager manager(dir.Write("bad.yml", R"(
rooms:
  - name: "Room"
    maxUsers: -1
    password: ""
)"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("room without a name")
  {
    ConfigManager manager(dir.Write("bad.yml", R"(
rooms:
  - maxUsers: 2
    password: ""
)"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("room without maxUsers")
  {
    ConfigManager manager(dir.Write("bad.yml", R"(
rooms:
  - name: "Room"
    password: ""
)"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("room without a password")
  {
    ConfigManager manager(dir.Write("bad.yml", R"(
rooms:
  - name: "Room"
    maxUsers: 2
)"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("non-boolean voiceEnabled")
  {
    ConfigManager manager(dir.Write("bad.yml", "voiceEnabled: maybe\n"));
    CHECK_FALSE(manager.Load());
  }
}

TEST_CASE("an empty file loads as all defaults")
{
  TempDir dir;
  ConfigManager manager(dir.Write("empty.yml", ""));
  REQUIRE(manager.Load());
  CHECK(manager.Get().port == 24816);
  CHECK(manager.Get().rooms.empty());
}

TEST_CASE("saved rooms, port and voice settings load back unchanged")
{
  TempDir dir;
  const auto path = dir.File("server-config.yml");

  ConfigManager writer(path);
  auto& config = writer.Get();
  config.port = 31337;
  config.voiceEnabled = false;
  config.rooms.push_back({.name = "One", .maxUsers = 5, .password = "secret", .voiceEnabled = true});
  config.rooms.push_back({.name = "Two \"quoted\"", .maxUsers = 1, .password = "", .voiceEnabled = false});
  REQUIRE(writer.Save());

  ConfigManager reader(path);
  REQUIRE(reader.Load());
  const auto& loaded = reader.Get();
  CHECK(loaded.port == 31337);
  CHECK_FALSE(loaded.voiceEnabled);
  REQUIRE(loaded.rooms.size() == 2);
  CHECK(loaded.rooms[0].name == "One");
  CHECK(loaded.rooms[0].maxUsers == 5);
  CHECK(loaded.rooms[0].password == "secret");
  CHECK(loaded.rooms[0].voiceEnabled);
  CHECK(loaded.rooms[1].name == "Two \"quoted\"");
  CHECK(loaded.rooms[1].maxUsers == 1);
  CHECK(loaded.rooms[1].password.empty());
  CHECK_FALSE(loaded.rooms[1].voiceEnabled);
}

TEST_CASE("saving into a missing directory fails")
{
  TempDir dir;
  ConfigManager manager(dir.File("missing/server-config.yml"));
  CHECK_FALSE(manager.Save());
}
}
