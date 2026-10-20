#pragma once

// Minimal base64 encode / decode helpers used to transfer binary image data
// across the saucer JS bridge (which operates on strings).

#include <cstdint>
#include <string>
#include <vector>

namespace Base64
{

inline std::string Encode(const std::uint8_t* data, std::size_t size)
{
  static constexpr char kTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string out;
  out.reserve(((size + 2) / 3) * 4);

  for (std::size_t i = 0; i < size; i += 3)
  {
    const std::uint32_t b0 = data[i];
    const std::uint32_t b1 = (i + 1 < size) ? data[i + 1] : 0u;
    const std::uint32_t b2 = (i + 2 < size) ? data[i + 2] : 0u;

    const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

    out += kTable[(triple >> 18) & 0x3F];
    out += kTable[(triple >> 12) & 0x3F];
    out += (i + 1 < size) ? kTable[(triple >> 6) & 0x3F] : '=';
    out += (i + 2 < size) ? kTable[(triple >> 0) & 0x3F] : '=';
  }

  return out;
}

inline std::string Encode(const std::vector<std::uint8_t>& data)
{
  return Encode(data.data(), data.size());
}

inline std::vector<std::uint8_t> Decode(const std::string& b64)
{
  static constexpr int8_t kLut[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  };

  std::vector<std::uint8_t> out;
  out.reserve((b64.size() / 4) * 3);

  std::uint32_t acc     = 0;
  int           accBits = 0;

  for (const unsigned char c : b64)
  {
    if (c == '=') break;
    const int val = kLut[c];
    if (val < 0) continue; // skip whitespace / unknown chars
    acc      = (acc << 6) | static_cast<std::uint32_t>(val);
    accBits += 6;
    if (accBits >= 8)
    {
      accBits -= 8;
      out.push_back(static_cast<std::uint8_t>((acc >> accBits) & 0xFF));
    }
  }

  return out;
}

} // namespace Base64
