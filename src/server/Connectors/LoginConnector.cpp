#include "LoginConnector.h"

#include <print>

#include "../Networking/ClientPacketHandler.h"
#include "../Networking/PeerSessionData.h"
#include "../Networking/ServerPacketFactory.h"
#include "../Rooms/ChatRoom.h"
#include "../Rooms/ChatRoomManager.h"
#include "../Rooms/User.h"

void LoginConnector::Connect(ClientPacketHandler& packetHandler, ChatRoomManager& chatRoomManager)
{
  packetHandler.onRoomEntryRequested.subscribe(
    [&chatRoomManager](ENetHost* server, ENetPeer* peer,
                       std::string roomName, std::string roomPassword, std::string userName)
    {
      // If the peer is already in a room, leave it first so a user can only
      // be in one room at a time.
      if (peer->data != nullptr)
      {
        auto* existingSession = static_cast<PeerSessionData*>(peer->data);
        ChatRoom* existingRoom = chatRoomManager.FindRoom(existingSession->roomName);
        if (existingRoom != nullptr)
        {
          existingRoom->RemoveUser(existingSession->userName);
          std::println("User '{}' left room '{}' to switch rooms.", existingSession->userName, existingSession->roomName);
          // Notify remaining peers in the old room about the updated user list
          ServerPacketFactory::BroadcastRoomUsersUpdate(server, existingSession->roomName,
                                                        existingRoom->GetUserNames());
        }
        delete existingSession;
        peer->data = nullptr;
      }

      ChatRoom* room = chatRoomManager.FindRoom(roomName);
      if (room == nullptr)
      {
        ServerPacketFactory::SendRoomEntryResponse(server, peer, false, "Room does not exist.");
        return;
      }

      if (!room->IsPasswordCorrect(roomPassword))
      {
        ServerPacketFactory::SendRoomEntryResponse(server, peer, false, "Room password is incorrect.");
        return;
      }

      if (room->IsUserLimitReached())
      {
        ServerPacketFactory::SendRoomEntryResponse(server, peer, false, "Room is full.");
        return;
      }

      if (room->HasUser(userName))
      {
        ServerPacketFactory::SendRoomEntryResponse(server, peer, false, "User name is already in use in this room.");
        return;
      }

      room->AddUser(User(userName, peer));

      // Tag the peer so we can find and remove the user on disconnect
      peer->data = new PeerSessionData{roomName, userName};

      std::println("Room entry granted: room='{}', user='{}'", roomName, userName);
      ServerPacketFactory::SendRoomEntryResponse(server, peer, true, "");
      ServerPacketFactory::SendRoomChatHistory(server, peer, room->GetHistory());

      // Broadcast the updated user list to all peers in the room (including the new one)
      ServerPacketFactory::BroadcastRoomUsersUpdate(server, roomName, room->GetUserNames());
    });

  packetHandler.onClientDisconnected.subscribe(
    [&chatRoomManager](ENetPeer* peer)
    {
      if (peer->data == nullptr)
      {
        std::println("An unauthenticated client disconnected.");
        return;
      }

      auto* session = static_cast<PeerSessionData*>(peer->data);
      peer->data = nullptr;

      ChatRoom* room = chatRoomManager.FindRoom(session->roomName);
      if (room != nullptr)
      {
        room->RemoveUser(session->userName);
        std::println("User '{}' removed from room '{}'.", session->userName, session->roomName);
        // Broadcast the updated user list to all remaining peers in the room
        ServerPacketFactory::BroadcastRoomUsersUpdate(peer->host, session->roomName,
                                                      room->GetUserNames());
      }

      delete session;
    });
}
