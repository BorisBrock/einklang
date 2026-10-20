#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// A single chat message, used by both the server (history storage) and the
/// client (history display).  The timestamp is stored as milliseconds since
/// the Unix epoch so that no chrono conversion is required when reading from
/// the database or decoding a wire packet.
struct ChatMessage
{
	enum class Kind : std::uint8_t { Text, Image };

	std::string  userName;
	std::string  text;            // non-empty for Kind::Text
	std::int64_t timestampMs{};   // milliseconds since Unix epoch
	Kind         kind{Kind::Text};
	std::vector<std::uint8_t> imageData; // non-empty for Kind::Image
};
