#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// Server status queries are connectionless: they are plain UDP datagrams sent
// to the server's ENet port, intercepted by the server before ENet's protocol
// handling.  This lets a client show online state and room occupancy for
// every known server without establishing (and logging) a full connection.
//
// Wire formats (all integer fields little-endian):
//
//  Client → Server (request), exactly kRequestSize bytes:
//   [4 bytes magic "EKSQ"]
//   [1 byte  version     ]       – kVersion
//   [4 bytes token       ]       – random, echoed back to match the reply
//   [N bytes zero padding]       – pads the request to kRequestSize
//
//  Server → Client (reply):
//   [4 bytes magic "EKSR"]
//   [4 bytes token       ]       – copied from the request
//   [N bytes payload     ]       – FlatBuffers ServerStatus (protocol.fbs)
//
// The request is padded and the reply size is capped so the reply cannot be
// abused for meaningful traffic amplification; the server additionally rate
// limits replies per source address.

namespace ServerQuery
{

inline constexpr std::array<std::uint8_t, 4> kRequestMagic{'E', 'K', 'S', 'Q'};
inline constexpr std::array<std::uint8_t, 4> kReplyMagic{'E', 'K', 'S', 'R'};
inline constexpr std::uint8_t kVersion = 1;

inline constexpr std::size_t kTokenOffset   = kRequestMagic.size() + 1;
inline constexpr std::size_t kRequestSize   = 64;
inline constexpr std::size_t kReplyHeaderSize = kReplyMagic.size() + sizeof(std::uint32_t);

// Upper bound for a reply datagram (stays below typical path MTUs).
inline constexpr std::size_t kMaxReplySize = 1200;

inline std::array<std::uint8_t, kRequestSize> BuildRequest(std::uint32_t token)
{
  std::array<std::uint8_t, kRequestSize> request{};
  std::memcpy(request.data(), kRequestMagic.data(), kRequestMagic.size());
  request[kRequestMagic.size()] = kVersion;
  std::memcpy(request.data() + kTokenOffset, &token, sizeof(token));
  return request;
}

// Returns true and extracts the token if data is a valid status request.
inline bool ParseRequest(std::span<const std::uint8_t> data, std::uint32_t& token)
{
  if (data.size() != kRequestSize ||
      std::memcmp(data.data(), kRequestMagic.data(), kRequestMagic.size()) != 0 ||
      data[kRequestMagic.size()] != kVersion)
    return false;

  std::memcpy(&token, data.data() + kTokenOffset, sizeof(token));
  return true;
}

// Returns true and extracts token and payload if data is a status reply.
inline bool ParseReply(std::span<const std::uint8_t> data, std::uint32_t& token,
                       std::span<const std::uint8_t>& payload)
{
  if (data.size() < kReplyHeaderSize ||
      std::memcmp(data.data(), kReplyMagic.data(), kReplyMagic.size()) != 0)
    return false;

  std::memcpy(&token, data.data() + kReplyMagic.size(), sizeof(token));
  payload = data.subspan(kReplyHeaderSize);
  return true;
}

} // namespace ServerQuery
