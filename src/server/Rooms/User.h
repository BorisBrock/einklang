#pragma once

#include <cstdint>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <enet.h>

class User
{
public:
	User(std::string name, ENetPeer* peer = nullptr);

	const std::string& Name() const;
	ENetPeer* Peer() const;

private:
	std::string mName;
	ENetPeer*   mPeer{};
};