#include "VoiceConnector.h"

#include <print>

#include "../Networking/ClientPacketHandler.h"
#include "../Networking/PeerSessionData.h"
#include "../Networking/ServerPacketFactory.h"
#include "../Rooms/ChatRoom.h"
#include "../Rooms/ChatRoomManager.h"

void VoiceConnector::Connect(ClientPacketHandler& packetHandler, ChatRoomManager& chatRoomManager)
{
  packetHandler.onVoiceFrameReceived.subscribe(
    [&chatRoomManager](ENetHost* server, ENetPeer* peer,
                       const std::uint8_t* voiceData, std::size_t voiceSize)
    {
      // Only authenticated peers may send voice
      if (peer->data == nullptr)
        return;

      const auto* session = static_cast<const PeerSessionData*>(peer->data);

      ChatRoom* room = chatRoomManager.FindRoom(session->roomName);
      if (room == nullptr)
        return;

      // Relay to every other peer in the same room
      const auto peers = room->GetUserPeers();
      ServerPacketFactory::RelayVoiceFrame(server, peers, peer, voiceData, voiceSize);
    });
}
