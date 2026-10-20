#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChatRoom.h"

struct sqlite3;

/// Thin wrapper around a SQLite database that persists chat messages.
/// One table per chat room is created with the naming convention "room_<name>".
class ChatDatabase
{
public:
	~ChatDatabase();

	/// Opens (or creates) the SQLite file at @p path.
	/// Returns true on success.
	bool Open(const std::string& path);

	void Close();

	/// Creates the message table for @p roomName if it does not already exist.
	/// Also adds the image_data column if the table already existed without it.
	void EnsureRoomTable(const std::string& roomName);

	/// Persists a single text message.  @p timestampMs is milliseconds since the
	/// Unix epoch.
	void InsertMessage(const std::string& roomName,
	                   const std::string& userName,
	                   const std::string& text,
	                   std::int64_t       timestampMs);

	/// Persists a single image message.
	void InsertImageMessage(const std::string&              roomName,
	                        const std::string&              userName,
	                        const std::vector<std::uint8_t>& imageData,
	                        std::int64_t                    timestampMs);

	/// Loads the most recent @p limit messages for @p roomName in insertion
	/// order. Pass 0 to load all messages.
	std::vector<ChatMessage> LoadMessages(const std::string& roomName, std::size_t limit = 0);

private:
	/// Returns a safe SQLite table name derived from @p roomName.
	static std::string TableName(const std::string& roomName);

	sqlite3* mDb{nullptr};
};
