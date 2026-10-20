#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "../../src/server/Rooms/ChatDatabase.h"
#include "TestUtils.h"

using TestUtils::TempDir;

namespace
{
void Exec(const std::string& path, const char* sql)
{
  sqlite3* db = nullptr;
  REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
  char* err = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK)
  {
    MESSAGE(err);
    sqlite3_free(err);
  }
  sqlite3_close(db);
  REQUIRE(rc == SQLITE_OK);
}
}

TEST_SUITE("ChatDatabase")
{
TEST_CASE("opening creates the database file")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));
  CHECK(std::filesystem::exists(dir.Path() / "chat.db"));
}

TEST_CASE("opening a path in a missing directory fails")
{
  TempDir dir;
  ChatDatabase db;
  CHECK_FALSE(db.Open(dir.File("missing/chat.db")));
}

TEST_CASE("an unopened database ignores writes and reads nothing")
{
  ChatDatabase db;
  db.EnsureRoomTable("Room");
  db.InsertMessage("Room", "alice", "hi", 1);
  db.InsertImageMessage("Room", "alice", {1, 2, 3}, 2);
  CHECK(db.LoadMessages("Room").empty());
}

TEST_CASE("text messages are stored and loaded in insertion order")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));
  db.EnsureRoomTable("Lobby");

  db.InsertMessage("Lobby", "alice", "first", 1000);
  db.InsertMessage("Lobby", "bob", "second", 999); // order follows insertion, not time
  db.InsertMessage("Lobby", "alice", "third", 3000);

  const auto messages = db.LoadMessages("Lobby");
  REQUIRE(messages.size() == 3);
  CHECK(messages[0].userName == "alice");
  CHECK(messages[0].text == "first");
  CHECK(messages[0].timestampMs == 1000);
  CHECK(messages[0].kind == ChatMessage::Kind::Text);
  CHECK(messages[0].imageData.empty());
  CHECK(messages[1].userName == "bob");
  CHECK(messages[1].text == "second");
  CHECK(messages[1].timestampMs == 999);
  CHECK(messages[2].text == "third");
}

TEST_CASE("the limit returns the most recent messages, oldest first")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));
  db.EnsureRoomTable("Lobby");
  for (int i = 0; i < 10; ++i)
    db.InsertMessage("Lobby", "u", std::to_string(i), i);

  SUBCASE("limit below the count")
  {
    const auto messages = db.LoadMessages("Lobby", 3);
    REQUIRE(messages.size() == 3);
    CHECK(messages[0].text == "7");
    CHECK(messages[1].text == "8");
    CHECK(messages[2].text == "9");
  }
  SUBCASE("limit equal to the count")
  {
    CHECK(db.LoadMessages("Lobby", 10).size() == 10);
  }
  SUBCASE("limit above the count")
  {
    const auto messages = db.LoadMessages("Lobby", 100);
    REQUIRE(messages.size() == 10);
    CHECK(messages.front().text == "0");
  }
  SUBCASE("zero means everything")
  {
    CHECK(db.LoadMessages("Lobby", 0).size() == 10);
  }
}

TEST_CASE("image messages keep their bytes, including zeros")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));
  db.EnsureRoomTable("Lobby");

  std::vector<std::uint8_t> image(4096);
  for (std::size_t i = 0; i < image.size(); ++i)
    image[i] = static_cast<std::uint8_t>(i * 31);
  image[0] = 0;

  db.InsertMessage("Lobby", "alice", "look:", 1);
  db.InsertImageMessage("Lobby", "alice", image, 2);

  const auto messages = db.LoadMessages("Lobby");
  REQUIRE(messages.size() == 2);
  CHECK(messages[0].kind == ChatMessage::Kind::Text);
  CHECK(messages[1].kind == ChatMessage::Kind::Image);
  CHECK(messages[1].userName == "alice");
  CHECK(messages[1].text.empty());
  CHECK(messages[1].timestampMs == 2);
  CHECK(messages[1].imageData == image);
}

TEST_CASE("text survives unicode, quotes and SQL-looking content")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));
  db.EnsureRoomTable("Lobby");

  const std::string user = "O'Brien \"the\" 🦊";
  const std::string text = "'); DROP TABLE \"room_Lobby\"; -- äöü 日本語\nline 2";
  db.InsertMessage("Lobby", user, text, 42);

  const auto messages = db.LoadMessages("Lobby");
  REQUIRE(messages.size() == 1);
  CHECK(messages[0].userName == user);
  CHECK(messages[0].text == text);
}

TEST_CASE("each room has its own history")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));
  db.EnsureRoomTable("Lobby");
  db.EnsureRoomTable("Game Room 1");

  db.InsertMessage("Lobby", "a", "in lobby", 1);
  db.InsertMessage("Game Room 1", "b", "in game room", 2);

  const auto lobby = db.LoadMessages("Lobby");
  const auto game = db.LoadMessages("Game Room 1");
  REQUIRE(lobby.size() == 1);
  REQUIRE(game.size() == 1);
  CHECK(lobby[0].text == "in lobby");
  CHECK(game[0].text == "in game room");
}

TEST_CASE("room names with special characters are safe table names")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));

  const std::string evil = "x\"; DROP TABLE room_Lobby; --";
  db.EnsureRoomTable("Lobby");
  db.InsertMessage("Lobby", "a", "keep me", 1);

  db.EnsureRoomTable(evil);
  db.InsertMessage(evil, "b", "evil room", 2);

  CHECK(db.LoadMessages("Lobby").size() == 1);
  const auto evilMessages = db.LoadMessages(evil);
  REQUIRE(evilMessages.size() == 1);
  CHECK(evilMessages[0].text == "evil room");
}

TEST_CASE("loading a room without a table returns nothing")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));
  CHECK(db.LoadMessages("Nowhere").empty());
  CHECK(db.LoadMessages("Nowhere", 5).empty());
}

TEST_CASE("EnsureRoomTable is idempotent and keeps existing messages")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));
  db.EnsureRoomTable("Lobby");
  db.InsertMessage("Lobby", "a", "hello", 1);
  db.EnsureRoomTable("Lobby");
  db.EnsureRoomTable("Lobby");
  CHECK(db.LoadMessages("Lobby").size() == 1);
}

TEST_CASE("messages persist across close and reopen")
{
  TempDir dir;
  const auto path = dir.File("chat.db");
  {
    ChatDatabase db;
    REQUIRE(db.Open(path));
    db.EnsureRoomTable("Lobby");
    db.InsertMessage("Lobby", "alice", "persisted", 123);
    db.InsertImageMessage("Lobby", "bob", {9, 8, 7}, 456);
  } // destructor closes

  ChatDatabase db;
  REQUIRE(db.Open(path));
  db.EnsureRoomTable("Lobby");
  const auto messages = db.LoadMessages("Lobby");
  REQUIRE(messages.size() == 2);
  CHECK(messages[0].text == "persisted");
  CHECK(messages[0].timestampMs == 123);
  CHECK(messages[1].imageData == std::vector<std::uint8_t>{9, 8, 7});
}

TEST_CASE("close is safe to call repeatedly and disables the database")
{
  TempDir dir;
  ChatDatabase db;
  REQUIRE(db.Open(dir.File("chat.db")));
  db.EnsureRoomTable("Lobby");
  db.Close();
  db.Close();
  db.InsertMessage("Lobby", "a", "dropped", 1);
  CHECK(db.LoadMessages("Lobby").empty());
}

TEST_CASE("tables from before image support are migrated")
{
  TempDir dir;
  const auto path = dir.File("chat.db");

  // Schema of databases written by older server versions
  Exec(path,
       "CREATE TABLE \"room_Lobby\" ("
       "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
       "  user_name    TEXT    NOT NULL,"
       "  text         TEXT    NOT NULL DEFAULT '',"
       "  timestamp_ms INTEGER NOT NULL);"
       "INSERT INTO \"room_Lobby\" (user_name, text, timestamp_ms) VALUES ('old', 'legacy', 7);");

  ChatDatabase db;
  REQUIRE(db.Open(path));
  db.EnsureRoomTable("Lobby");
  db.InsertImageMessage("Lobby", "new", {1, 2}, 8);

  const auto messages = db.LoadMessages("Lobby");
  REQUIRE(messages.size() == 2);
  CHECK(messages[0].userName == "old");
  CHECK(messages[0].text == "legacy");
  CHECK(messages[0].kind == ChatMessage::Kind::Text);
  CHECK(messages[1].kind == ChatMessage::Kind::Image);
  CHECK(messages[1].imageData == std::vector<std::uint8_t>{1, 2});
}
}
