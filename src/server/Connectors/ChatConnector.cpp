#include "ChatConnector.h"

#include <print>
#include <vector>

#include "../Networking/ClientPacketHandler.h"
#include "../Networking/PeerSessionData.h"
#include "../Networking/ServerPacketFactory.h"
#include "../Rooms/ChatRoomManager.h"

void ChatConnector::Connect(ClientPacketHandler& packetHandler, ChatRoomManager& chatRoomManager)
{
  packetHandler.onChatMessageReceived.subscribe([&chatRoomManager](ENetHost* server, ENetPeer* peer, std::string text)
  {
    if (peer->data == nullptr)
    {
      std::println("[Chat] Received message from unauthenticated peer; ignoring.");
      return;
    }

    const auto* session = static_cast<const PeerSessionData*>(peer->data);
    std::println("[Chat] [{}] {}: {}", session->roomName, session->userName, text);

    ChatRoom* room = chatRoomManager.FindRoom(session->roomName);
    if (room != nullptr)
    {
      chatRoomManager.AddMessage(session->roomName, session->userName, text);
    }

    ServerPacketFactory::BroadcastNewChatMessage(server, session->roomName, session->userName, text);
  });

  packetHandler.onChatImageReceived.subscribe([&chatRoomManager](ENetHost* server, ENetPeer* peer,
                                                                   std::vector<std::uint8_t> imageData)
  {
    if (peer->data == nullptr)
    {
      std::println("[Chat] Received image from unauthenticated peer; ignoring.");
      return;
    }

    const auto* session = static_cast<const PeerSessionData*>(peer->data);
    std::println("[Chat] [{}] {} sent an image ({} bytes).", session->roomName, session->userName, imageData.size());

    ChatRoom* room = chatRoomManager.FindRoom(session->roomName);
    if (room != nullptr)
    {
      chatRoomManager.AddImageMessage(session->roomName, session->userName, imageData);
    }

    ServerPacketFactory::BroadcastNewChatImage(server, session->roomName, session->userName, imageData);
  });
}
