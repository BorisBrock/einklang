#pragma once

class ClientPacketHandler;
class ChatRoomManager;

class LoginConnector
{
public:
  static void Connect(ClientPacketHandler& packetHandler, ChatRoomManager& chatRoomManager);
};
