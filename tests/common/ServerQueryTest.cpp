#include <doctest.h>

#include <cstdint>
#include <vector>

#include "../../src/common/ServerQuery.h"

TEST_SUITE("ServerQuery")
{
TEST_CASE("a built request has the documented layout and parses back")
{
  const std::uint32_t token = 0xDEADBEEF;
  const auto request = ServerQuery::BuildRequest(token);

  REQUIRE(request.size() == ServerQuery::kRequestSize);
  CHECK(request[0] == 'E');
  CHECK(request[1] == 'K');
  CHECK(request[2] == 'S');
  CHECK(request[3] == 'Q');
  CHECK(request[4] == ServerQuery::kVersion);
  for (std::size_t i = ServerQuery::kTokenOffset + sizeof(token); i < request.size(); ++i)
    CHECK(request[i] == 0);

  std::uint32_t parsed = 0;
  REQUIRE(ServerQuery::ParseRequest(request, parsed));
  CHECK(parsed == token);
}

TEST_CASE("requests with a wrong size, magic or version are rejected")
{
  std::uint32_t token = 0;
  auto request = ServerQuery::BuildRequest(1);

  SUBCASE("too short")
  {
    CHECK_FALSE(ServerQuery::ParseRequest({request.data(), request.size() - 1}, token));
  }
  SUBCASE("too long")
  {
    std::vector<std::uint8_t> longer(request.begin(), request.end());
    longer.push_back(0);
    CHECK_FALSE(ServerQuery::ParseRequest(longer, token));
  }
  SUBCASE("wrong magic")
  {
    request[3] = 'R';
    CHECK_FALSE(ServerQuery::ParseRequest(request, token));
  }
  SUBCASE("wrong version")
  {
    request[4] = ServerQuery::kVersion + 1;
    CHECK_FALSE(ServerQuery::ParseRequest(request, token));
  }
  SUBCASE("empty")
  {
    CHECK_FALSE(ServerQuery::ParseRequest({}, token));
  }
}

TEST_CASE("replies are parsed into token and payload")
{
  std::vector<std::uint8_t> reply{'E', 'K', 'S', 'R', 0x78, 0x56, 0x34, 0x12, 9, 8, 7};
  std::uint32_t token = 0;
  std::span<const std::uint8_t> payload;

  REQUIRE(ServerQuery::ParseReply(reply, token, payload));
  CHECK(token == 0x12345678);
  REQUIRE(payload.size() == 3);
  CHECK(payload[0] == 9);
  CHECK(payload[2] == 7);
}

TEST_CASE("a reply without payload is valid")
{
  std::vector<std::uint8_t> reply{'E', 'K', 'S', 'R', 1, 0, 0, 0};
  std::uint32_t token = 0;
  std::span<const std::uint8_t> payload;
  REQUIRE(ServerQuery::ParseReply(reply, token, payload));
  CHECK(token == 1);
  CHECK(payload.empty());
}

TEST_CASE("truncated replies and requests are not mistaken for replies")
{
  std::uint32_t token = 0;
  std::span<const std::uint8_t> payload;

  std::vector<std::uint8_t> truncated{'E', 'K', 'S', 'R', 1, 0, 0};
  CHECK_FALSE(ServerQuery::ParseReply(truncated, token, payload));

  const auto request = ServerQuery::BuildRequest(1);
  CHECK_FALSE(ServerQuery::ParseReply(request, token, payload));
}
}
