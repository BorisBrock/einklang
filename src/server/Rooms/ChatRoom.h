#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../../common/ChatMessage.h"
#include "User.h"

class ChatRoom
{
public:
	ChatRoom(std::string name, std::uint16_t maxUsers, std::string password,
	         std::uint32_t maxHistory = 500);

	const std::string& Name() const;
	std::uint16_t MaxUsers() const;
	std::uint32_t MaxHistory() const;
	const std::string& Password() const;
	bool IsPasswordCorrect(std::string_view password) const;
	bool IsUserLimitReached() const;
	bool HasUser(std::string_view userName) const;
	void AddUser(User user);
	void RemoveUser(std::string_view userName);

	// Records a text message in the room history.
	void AddMessage(std::string userName, std::string text, std::int64_t timestampMs);

	// Records an image message in the room history.
	void AddImageMessage(std::string userName, std::vector<std::uint8_t> imageData,
	                     std::int64_t timestampMs);

	// Returns the number of users currently in the room
	std::size_t UserCount() const;

	// Returns the name of every user currently in the room
	std::vector<std::string> GetUserNames() const;

	// Returns the ENetPeer* of every user currently in the room
	std::vector<ENetPeer*> GetUserPeers() const;

	// Returns the full chat history for this room
	const std::vector<ChatMessage>& GetHistory() const;

private:
	std::string mName;
	std::uint16_t mMaxUsers{};
	std::string mPassword;
	std::uint32_t mMaxHistory{500};
	std::vector<User> mUsers;
	std::vector<ChatMessage> mHistory;
};