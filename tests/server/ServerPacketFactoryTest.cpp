#include <doctest.h>

#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include "../../src/common/ServerQuery.h"
#include "../../src/server/Networking/ServerPacketFactory.h"
#include "protocol_generated.h"

namespace
{
const Einklang::ServerStatus* Parse(const std::vector<std::uint8_t>& payload)
{
  flatbuffers::Verifier verifier(payload.data(), payload.size());
  REQUIRE(verifier.VerifyBuffer<Einklang::ServerStatus>(nullptr));
  return flatbuffers::GetRoot<Einklang::ServerStatus>(payload.data());
}
}

TEST_SUITE("ServerPacketFactory")
{
TEST_CASE("the status payload describes every room")
{
  std::vector<ChatRoom> rooms;
  rooms.emplace_back("Lobby", 10, "secret");
  rooms.emplace_back("Open", 3, "");
  rooms[0].AddUser(User("alice"));
  rooms[0].AddUser(User("bob"));

  const auto payload = ServerPacketFactory::BuildServerStatus(rooms);
  const auto* status = Parse(payload);

  REQUIRE(status->server_version() != nullptr);
  CHECK(status->server_version()->str() == EINKLANG_VERSION_STRING);
  REQUIRE(status->rooms() != nullptr);
  REQUIRE(status->rooms()->size() == 2);

  const auto* lobby = status->rooms()->Get(0);
  CHECK(lobby->name()->str() == "Lobby");
  CHECK(lobby->user_count() == 2);
  CHECK(lobby->max_users() == 10);
  CHECK(lobby->has_password());

  const auto* open = status->rooms()->Get(1);
  CHECK(open->name()->str() == "Open");
  CHECK(open->user_count() == 0);
  CHECK(open->max_users() == 3);
  CHECK_FALSE(open->has_password());
}

TEST_CASE("the status payload never reveals passwords")
{
  std::vector<ChatRoom> rooms;
  rooms.emplace_back("Lobby", 10, "VerySecretPassword");
  const auto payload = ServerPacketFactory::BuildServerStatus(rooms);
  const std::string bytes(payload.begin(), payload.end());
  CHECK(bytes.find("VerySecretPassword") == std::string::npos);
}

TEST_CASE("a server without rooms still reports its version")
{
  const auto payload = ServerPacketFactory::BuildServerStatus({});
  const auto* status = Parse(payload);
  CHECK(status->server_version()->str() == EINKLANG_VERSION_STRING);
  CHECK((status->rooms() == nullptr || status->rooms()->size() == 0));
}

TEST_CASE("rooms are dropped from the end to fit the reply size cap")
{
  std::vector<ChatRoom> rooms;
  for (int i = 0; i < 100; ++i)
    rooms.emplace_back("Room number " + std::to_string(i) + std::string(40, '.'), 5, "");

  const auto payload = ServerPacketFactory::BuildServerStatus(rooms);
  CHECK(payload.size() <= ServerQuery::kMaxReplySize - ServerQuery::kReplyHeaderSize);

  const auto* status = Parse(payload);
  REQUIRE(status->rooms() != nullptr);
  const auto count = status->rooms()->size();
  CHECK(count > 0);
  CHECK(count < rooms.size());
  for (flatbuffers::uoffset_t i = 0; i < count; ++i)
    CHECK(status->rooms()->Get(i)->name()->str() == rooms[i].Name());
}
}
