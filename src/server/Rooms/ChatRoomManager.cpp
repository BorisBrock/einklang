#include "ChatRoomManager.h"

#include <chrono>
#include <print>

#include "../Configuration/ServerConfig.h"

void ChatRoomManager::InitializeFromConfig(const ServerConfig& config)
{
	mRooms.clear();
	mRooms.reserve(config.rooms.size());

	for (const auto& roomConfig : config.rooms)
	{
		mRooms.emplace_back(roomConfig.name, roomConfig.maxUsers, roomConfig.password,
		                    config.maxHistoryMessages);
	}
}

void ChatRoomManager::OpenDatabase(const std::string& path)
{
	std::println("[DB] Opening database at '{}'...", path);
	if (!mDatabase.Open(path))
	{
		std::println("[DB] Database unavailable. Ensure the path is writable and the directory exists.");
		return;
	}

	for (auto& room : mRooms)
	{
		mDatabase.EnsureRoomTable(room.Name());
		auto messages = mDatabase.LoadMessages(room.Name(), room.MaxHistory());
		std::println("[DB] Loaded {} message(s) for room '{}'.", messages.size(), room.Name());
		for (auto& msg : messages)
		{
			if (msg.kind == ChatMessage::Kind::Image)
				room.AddImageMessage(msg.userName, std::move(msg.imageData), msg.timestampMs);
			else
				room.AddMessage(msg.userName, msg.text, msg.timestampMs);
		}
	}
}

void ChatRoomManager::AddMessage(const std::string& roomName,
                                  const std::string& userName,
                                  const std::string& text)
{
	ChatRoom* room = FindRoom(roomName);
	if (!room)
		return;

	const auto now = std::chrono::system_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()).count();
	room->AddMessage(userName, text, ms);

	mDatabase.InsertMessage(roomName, userName, text, ms);
}

void ChatRoomManager::AddImageMessage(const std::string&        roomName,
                                       const std::string&        userName,
                                       std::vector<std::uint8_t> imageData)
{
	ChatRoom* room = FindRoom(roomName);
	if (!room)
		return;

	const auto now = std::chrono::system_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()).count();
	room->AddImageMessage(userName, imageData, ms);

	mDatabase.InsertImageMessage(roomName, userName, imageData, ms);
}

ChatRoom* ChatRoomManager::FindRoom(std::string_view roomName)
{
	for (auto& room : mRooms)
	{
		if (room.Name() == roomName)
		{
			return &room;
		}
	}

	return nullptr;
}

const std::vector<ChatRoom>& ChatRoomManager::Rooms() const
{
	return mRooms;
}