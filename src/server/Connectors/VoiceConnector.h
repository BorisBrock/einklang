#pragma once

class ClientPacketHandler;
class ChatRoomManager;

// Wires the server's voice-frame receive event to the room-based relay logic.
class VoiceConnector
{
public:
  static void Connect(ClientPacketHandler& packetHandler, ChatRoomManager& chatRoomManager);
};
