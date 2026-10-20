#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ChatDatabase.h"
#include "ChatRoom.h"

struct ServerConfig;

class ChatRoomManager
{
public:
	void InitializeFromConfig(const ServerConfig& config);

	/// Opens (or creates) the SQLite database at @p path, creates per-room
	/// tables, and pre-populates each room's in-memory history from the DB.
	/// Must be called after InitializeFromConfig.
	void OpenDatabase(const std::string& path);

	ChatRoom* FindRoom(std::string_view roomName);

	/// Adds a text message to the in-memory room history AND persists it to the DB.
	void AddMessage(const std::string& roomName,
	                const std::string& userName,
	                const std::string& text);

	/// Adds an image message to the in-memory room history AND persists it to the DB.
	void AddImageMessage(const std::string&              roomName,
	                     const std::string&              userName,
	                     std::vector<std::uint8_t>       imageData);

	const std::vector<ChatRoom>& Rooms() const;

private:
	std::vector<ChatRoom> mRooms;
	ChatDatabase          mDatabase;
};