#include "WebViewModel.h"

#include <algorithm>
#include <format>

#include "JsUtils.h"

void WebViewModel::SetKnownRooms(std::vector<ClientConfig::KnownRoom> rooms)
{
  mKnownRooms = std::move(rooms);
}

void WebViewModel::AddKnownRoom(const ClientConfig::KnownRoom& room)
{
  const bool exists = std::ranges::any_of(mKnownRooms, [&](const ClientConfig::KnownRoom& r)
  {
    return r.address == room.address && r.port == room.port && r.roomName == room.roomName;
  });

  if (exists)
    return;

  mKnownRooms.push_back(room);
  onRoomAdded.emit(room);
}

void WebViewModel::NotifyRoomEntryResponse(bool success, const std::string& errorMessage)
{
  onRoomEntryResult.emit(success, errorMessage);
}

std::string WebViewModel::GetKnownRoomsJson() const
{
  return BuildRoomsJson(mKnownRooms);
}

void WebViewModel::HandleChatMessage(std::string msg)
{
  onChatMessageSubmitted.emit(std::move(msg));
}

void WebViewModel::HandleChatImage(std::vector<std::uint8_t> imageData)
{
  onChatImageSubmitted.emit(std::move(imageData));
}

void WebViewModel::HandleConnectToServer(std::string server, uint16_t port, std::string room,
                                         std::string password, std::string username)
{
  mLocalUserName = username;
  onConnectToServer.emit(std::move(server), port, std::move(room),
                         std::move(password), std::move(username));
}

void WebViewModel::HandleVoiceMuteToggle(bool muted)
{
  onVoiceMuteToggled.emit(muted);
}

void WebViewModel::HandleSetVoiceSettings(float vadThreshold, float inputGain)
{
  onVoiceSettingsChanged.emit(vadThreshold, inputGain);
}

void WebViewModel::SetInputRmsGetter(std::function<float()> fn)
{
  mInputRmsGetter = std::move(fn);
}

float WebViewModel::GetInputRms() const
{
  return mInputRmsGetter ? mInputRmsGetter() : 0.0f;
}

void WebViewModel::HandleSwitchToRoom(std::string address, uint16_t port,
                                      std::string roomName, std::string userName)
{
  mLocalUserName = userName;
  onSwitchToRoom.emit(std::move(address), port, std::move(roomName), std::move(userName));
}

void WebViewModel::HandleRemoveRoom(std::string address, uint16_t port, std::string roomName)
{
  const bool wasActive = (mActiveAddress == address &&
                          mActivePort    == port    &&
                          mActiveRoom    == roomName);

  std::erase_if(mKnownRooms, [&](const ClientConfig::KnownRoom& r)
  {
    return r.address == address && r.port == port && r.roomName == roomName;
  });

  if (wasActive)
  {
    mActiveAddress.clear();
    mActivePort = 0;
    mActiveRoom.clear();
    onActiveRoomLeft.emit();
  }
}

void WebViewModel::HandleRemoveServer(std::string address, uint16_t port)
{
  const bool activeWasHere = (mActiveAddress == address && mActivePort == port);

  std::erase_if(mKnownRooms, [&](const ClientConfig::KnownRoom& r)
  {
    return r.address == address && r.port == port;
  });

  if (activeWasHere)
  {
    mActiveAddress.clear();
    mActivePort = 0;
    mActiveRoom.clear();
    onActiveRoomLeft.emit();
  }
}

void WebViewModel::SetActiveRoom(const std::string& address, uint16_t port,
                                 const std::string& roomName)
{
  mActiveAddress = address;
  mActivePort    = port;
  mActiveRoom    = roomName;
  onActiveRoomChanged.emit(address, port, roomName);
}

void WebViewModel::NotifyNewChatMessage(const std::string& userName, const std::string& text)
{
  onNewChatMessage.emit(userName, text);
}

void WebViewModel::NotifyNewChatImage(const std::string& userName,
                                       std::vector<std::uint8_t> imageData)
{
  onNewChatImage.emit(userName, std::move(imageData));
}

void WebViewModel::NotifyRoomUsersUpdate(std::vector<std::string> userNames)
{
  onRoomUsersUpdate.emit(std::move(userNames));
}

void WebViewModel::NotifyUserSpeaking(const std::string& userName, bool isSpeaking)
{
  onUserSpeaking.emit(userName, isSpeaking);
}

void WebViewModel::NotifyRoomChatHistory(std::vector<ChatMessage> entries)
{
  onRoomChatHistory.emit(std::move(entries));
}

// static
std::string WebViewModel::BuildRoomsJson(const std::vector<ClientConfig::KnownRoom>& rooms)
{
  std::string json = "[";
  for (std::size_t i = 0; i < rooms.size(); ++i)
  {
    if (i > 0)
      json += ",";
    json += std::format(
      R"({{"address":"{}","port":{},"roomName":"{}","userName":"{}"}})",
      JsUtils::EscapeString(rooms[i].address),
      rooms[i].port,
      JsUtils::EscapeString(rooms[i].roomName),
      JsUtils::EscapeString(rooms[i].userName));
  }
  json += "]";
  return json;
}
