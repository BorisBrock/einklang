#pragma once

class ClientPacketHandler;
class ChatRoomManager;

class ChatConnector
{
public:
  static void Connect(ClientPacketHandler& packetHandler, ChatRoomManager& chatRoomManager);
};
