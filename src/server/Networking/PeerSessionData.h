#pragma once

#include <string>

// Heap-allocated data stored in ENetPeer::data for each authenticated peer.
// Created by LoginConnector on successful room entry; deleted on disconnect.
struct PeerSessionData
{
  std::string roomName;
  std::string userName;
};
