#include "ChatRoom.h"

#include <algorithm>

ChatRoom::ChatRoom(std::string name, std::uint16_t maxUsers, std::string password,
                   std::uint32_t maxHistory)
	: mName(std::move(name)),
		mMaxUsers(maxUsers),
		mPassword(std::move(password)),
		mMaxHistory(maxHistory)
{
}

const std::string& ChatRoom::Name() const
{
	return mName;
}

std::uint16_t ChatRoom::MaxUsers() const
{
	return mMaxUsers;
}

std::uint32_t ChatRoom::MaxHistory() const
{
	return mMaxHistory;
}

const std::string& ChatRoom::Password() const
{
	return mPassword;
}

bool ChatRoom::IsPasswordCorrect(std::string_view password) const
{
	return mPassword == password;
}

bool ChatRoom::IsUserLimitReached() const
{
	return mUsers.size() >= mMaxUsers;
}

bool ChatRoom::HasUser(std::string_view userName) const
{
	return std::ranges::any_of(mUsers, [userName](const User& user)
	{
		return user.Name() == userName;
	});
}

void ChatRoom::AddUser(User user)
{
	mUsers.push_back(std::move(user));
}

void ChatRoom::RemoveUser(std::string_view userName)
{
	std::erase_if(mUsers, [userName](const User& user)
	{
		return user.Name() == userName;
	});
}

std::size_t ChatRoom::UserCount() const
{
	return mUsers.size();
}

std::vector<std::string> ChatRoom::GetUserNames() const
{
	std::vector<std::string> names;
	names.reserve(mUsers.size());
	for (const auto& user : mUsers)
		names.push_back(user.Name());
	return names;
}

std::vector<ENetPeer*> ChatRoom::GetUserPeers() const
{
	std::vector<ENetPeer*> peers;
	peers.reserve(mUsers.size());
	for (const auto& user : mUsers)
	{
		if (user.Peer() != nullptr)
			peers.push_back(user.Peer());
	}
	return peers;
}

void ChatRoom::AddMessage(std::string userName, std::string text,
                          std::int64_t timestampMs)
{
	if (mMaxHistory > 0 && mHistory.size() >= mMaxHistory)
		mHistory.erase(mHistory.begin());

	mHistory.push_back(ChatMessage{
		.userName    = std::move(userName),
		.text        = std::move(text),
		.timestampMs = timestampMs,
		.kind        = ChatMessage::Kind::Text
	});
}

void ChatRoom::AddImageMessage(std::string userName, std::vector<std::uint8_t> imageData,
                               std::int64_t timestampMs)
{
	if (mMaxHistory > 0 && mHistory.size() >= mMaxHistory)
		mHistory.erase(mHistory.begin());

	mHistory.push_back(ChatMessage{
		.userName    = std::move(userName),
		.text        = {},
		.timestampMs = timestampMs,
		.kind        = ChatMessage::Kind::Image,
		.imageData   = std::move(imageData)
	});
}

const std::vector<ChatMessage>& ChatRoom::GetHistory() const
{
	return mHistory;
}