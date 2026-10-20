#pragma once

#include <cstddef>
#include <cstdint>

// Voice packets use a compact binary layout instead of FlatBuffers to minimise
// overhead on the high-frequency, latency-sensitive audio path.
//
// Wire formats (all fields little-endian):
//
//  Client → Server:
//   [1 byte  PacketType    ]       – same type-byte prefix as all other packets
//   [4 bytes sequenceNumber]       – monotonically increasing per sender
//   [N bytes opusPayload   ]       – raw Opus-encoded audio frame
//
//  Server → Client (relay):
//   [1 byte  ServerPacketType]     – same type-byte prefix as all other packets
//   [4 bytes senderPeerId   ]      – ENetPeer::connectID of the original sender
//   [1 byte  senderNameLen  ]      – UTF-8 byte length of sender name (0..255)
//   [M bytes senderName     ]      – sender name bytes (not null-terminated)
//   [4 bytes sequenceNumber ]      – original sequence number from the sender
//   [N bytes opusPayload    ]      – raw Opus-encoded audio frame (unchanged)
//
// The server does NOT decode the Opus payload; it reads only the type byte,
// injects the sender's connectID, and relays the frame to every other peer in
// the same room.  Each client maintains one OpusDecoder per remote senderPeerId
// (because Opus decoders are stateful) and mixes the decoded PCM streams at
// playback time.
//
// ENet channel: 2 (unreliable / unsequenced).

namespace Voice
{

// Dedicated ENet channel for voice traffic (unreliable)
inline constexpr std::uint8_t kVoiceChannel = 2;

// ENet packet-throttle parameters applied to every peer.
//
// By default ENet *randomly drops unreliable packets* whenever the measured
// round-trip time rises above its recent average (its throttle decelerates
// by 2/32 per RTT spike).  Every voice packet is unreliable, so ordinary
// Wi-Fi or internet jitter turned directly into missing audio.  Voice has its
// own loss handling (FEC + PLC + jitter buffer), so the throttle is pinned
// fully open: acceleration = max, deceleration = 0.
inline constexpr std::uint32_t kThrottleInterval     = 5000; // ms (ENet default)
inline constexpr std::uint32_t kThrottleAcceleration = 32;   // ENET_PEER_PACKET_THROTTLE_SCALE
inline constexpr std::uint32_t kThrottleDeceleration = 0;

// Opus constants
inline constexpr int kSampleRate  = 48000;  // Hz
inline constexpr int kChannels    = 1;      // mono
inline constexpr int kFrameMs     = 20;     // milliseconds per frame
inline constexpr int kFrameSize   = kSampleRate * kFrameMs / 1000; // 960 samples

// Maximum encoded Opus frame size (upper bound defined by RFC 6716).
inline constexpr std::size_t kMaxEncodedBytes = 1275;

#pragma pack(push, 1)
// Header on client → server voice packets.
struct VoiceFrameHeader
{
  std::uint32_t sequenceNumber;
};

#pragma pack(pop)

inline constexpr std::size_t kHeaderSize      = sizeof(VoiceFrameHeader);

// Relay packet metadata prefix sizes (server → client).
inline constexpr std::size_t kRelaySenderPeerIdSize   = sizeof(std::uint32_t);
inline constexpr std::size_t kRelaySenderNameLenSize  = sizeof(std::uint8_t);
inline constexpr std::size_t kRelayPrefixSize         = kRelaySenderPeerIdSize + kRelaySenderNameLenSize;

// Minimum valid relay payload:
// senderPeerId + senderNameLen + sequenceNumber + at least 1 byte Opus.
inline constexpr std::size_t kRelayMinPayloadSize =
  kRelayPrefixSize + kHeaderSize + 1;

} // namespace Voice
