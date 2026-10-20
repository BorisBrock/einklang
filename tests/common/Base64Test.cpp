#include <doctest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "../../src/common/Base64.h"

namespace
{
std::vector<std::uint8_t> Bytes(const std::string& s)
{
  return {s.begin(), s.end()};
}
}

TEST_SUITE("Base64")
{
TEST_CASE("encodes the RFC 4648 test vectors")
{
  CHECK(Base64::Encode(Bytes("")) == "");
  CHECK(Base64::Encode(Bytes("f")) == "Zg==");
  CHECK(Base64::Encode(Bytes("fo")) == "Zm8=");
  CHECK(Base64::Encode(Bytes("foo")) == "Zm9v");
  CHECK(Base64::Encode(Bytes("foob")) == "Zm9vYg==");
  CHECK(Base64::Encode(Bytes("fooba")) == "Zm9vYmE=");
  CHECK(Base64::Encode(Bytes("foobar")) == "Zm9vYmFy");
}

TEST_CASE("decodes the RFC 4648 test vectors")
{
  CHECK(Base64::Decode("") == Bytes(""));
  CHECK(Base64::Decode("Zg==") == Bytes("f"));
  CHECK(Base64::Decode("Zm8=") == Bytes("fo"));
  CHECK(Base64::Decode("Zm9v") == Bytes("foo"));
  CHECK(Base64::Decode("Zm9vYg==") == Bytes("foob"));
  CHECK(Base64::Decode("Zm9vYmE=") == Bytes("fooba"));
  CHECK(Base64::Decode("Zm9vYmFy") == Bytes("foobar"));
}

TEST_CASE("uses + and / for the last two alphabet characters")
{
  const std::vector<std::uint8_t> data{0xFB, 0xFF, 0xBF};
  CHECK(Base64::Encode(data) == "+/+/");
  CHECK(Base64::Decode("+/+/") == data);
}

TEST_CASE("skips whitespace and unknown characters while decoding")
{
  CHECK(Base64::Decode("Zm9v\nYmFy") == Bytes("foobar"));
  CHECK(Base64::Decode(" Zm 9v YmFy ") == Bytes("foobar"));
}

TEST_CASE("round-trips arbitrary binary data of every length")
{
  std::mt19937 rng(1234);
  std::uniform_int_distribution<int> byte(0, 255);
  for (std::size_t size = 0; size < 300; ++size)
  {
    std::vector<std::uint8_t> data(size);
    for (auto& b : data)
      b = static_cast<std::uint8_t>(byte(rng));

    const auto encoded = Base64::Encode(data);
    CHECK(encoded.size() == (size + 2) / 3 * 4);
    CHECK(Base64::Decode(encoded) == data);
  }
}
}
