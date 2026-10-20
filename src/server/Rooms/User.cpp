#include "User.h"

User::User(std::string name, ENetPeer* peer)
	: mName(std::move(name)),
	  mPeer(peer)
{
}

const std::string& User::Name() const
{
	return mName;
}

ENetPeer* User::Peer() const
{
	return mPeer;
}
