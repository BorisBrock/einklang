#include "WebViewModel.h"

#include <algorithm>
#include <format>

#include "JsUtils.h"

void WebViewModel::SetKnownRooms(std::vector<ClientConfig::KnownRoom> rooms)
{
  mKnownRooms = std::move(rooms);
}

void WebViewModel::SetUserName(std::string userName)
{
  mUserName = std::move(userName);
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
  onKnownRoomsChanged.emit();
}

void WebViewModel::NotifyRoomEntryResponse(bool success, const std::string& errorMessage)
{
  if (mReconnecting)
  {
    if (!success)
    {
      onReconnectAttemptFailed.emit(errorMessage);
      return;
    }
    EndReconnect();
  }

  onRoomEntryResult.emit(success, errorMessage);
}

bool WebViewModel::BeginReconnect()
{
  if (mActiveAddress.empty() || mReconnecting.exchange(true))
    return false;

  onReconnectingChanged.emit(true);
  return true;
}

void WebViewModel::ReconnectToActiveRoom()
{
  if (!mReconnecting || mActiveAddress.empty())
    return;

  onSwitchToRoom.emit(mActiveAddress, mActivePort, mActiveRoom, mLocalUserName);
}

bool WebViewModel::EndReconnect()
{
  if (!mReconnecting.exchange(false))
    return false;

  onReconnectingChanged.emit(false);
  return true;
}

void WebViewModel::CancelReconnectForUserAction()
{
  if (!EndReconnect())
    return;

  mActiveAddress.clear();
  mActivePort = 0;
  mActiveRoom.clear();
  onReconnectCancelled.emit();
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
                                         std::string password)
{
  CancelReconnectForUserAction();
  mLocalUserName = mUserName;
  onConnectToServer.emit(std::move(server), port, std::move(room),
                         std::move(password), mUserName);
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

void WebViewModel::HandleSwitchToRoom(std::string address, uint16_t port, std::string roomName)
{
  CancelReconnectForUserAction();
  mLocalUserName = mUserName;
  onSwitchToRoom.emit(std::move(address), port, std::move(roomName), mUserName);
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
  onKnownRoomsChanged.emit();

  if (wasActive)
  {
    EndReconnect();
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
  onKnownRoomsChanged.emit();

  if (activeWasHere)
  {
    EndReconnect();
    mActiveAddress.clear();
    mActivePort = 0;
    mActiveRoom.clear();
    onActiveRoomLeft.emit();
  }
}

std::string WebViewModel::HandleEditServer(const std::string& oldAddress, uint16_t oldPort,
                                           const std::string& newAddress, uint16_t newPort)
{
  const bool activeWasHere = (mActiveAddress == oldAddress && mActivePort == oldPort);

  const bool changed = EditServerRooms(mKnownRooms, oldAddress, oldPort, newAddress, newPort);
  if (changed)
    onKnownRoomsChanged.emit();

  // The current connection uses the old settings — drop it so the next
  // join uses the edited ones.
  if (changed && activeWasHere)
  {
    EndReconnect();
    mActiveAddress.clear();
    mActivePort = 0;
    mActiveRoom.clear();
    onActiveRoomLeft.emit();
  }

  return BuildRoomsJson(mKnownRooms);
}

// static
bool WebViewModel::EditServerRooms(std::vector<ClientConfig::KnownRoom>& rooms,
                                   const std::string& oldAddress, uint16_t oldPort,
                                   const std::string& newAddress, uint16_t newPort)
{
  const bool sameServer = (oldAddress == newAddress && oldPort == newPort);
  bool changed = false;

  std::vector<ClientConfig::KnownRoom> result;
  result.reserve(rooms.size());

  for (auto& r : rooms)
  {
    const bool onServer = (r.address == oldAddress && r.port == oldPort);
    if (onServer && !sameServer)
    {
      r.address = newAddress;
      r.port    = newPort;
      changed   = true;
    }

    // When moving onto a server that is already known, merging can produce
    // duplicate rooms; keep the first occurrence.
    const bool duplicate = std::ranges::any_of(result, [&](const ClientConfig::KnownRoom& o)
    {
      return o.address == r.address && o.port == r.port && o.roomName == r.roomName;
    });
    if (duplicate)
      continue;

    result.push_back(std::move(r));
  }

  rooms = std::move(result);
  return changed;
}

void WebViewModel::SetActiveRoom(const std::string& address, uint16_t port,
                                 const std::string& roomName, const std::string& userName)
{
  mActiveAddress = address;
  mActivePort    = port;
  mActiveRoom    = roomName;
  onActiveRoomChanged.emit(address, port, roomName, userName);
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

std::vector<std::pair<std::string, uint16_t>> WebViewModel::GetKnownServers() const
{
  std::vector<std::pair<std::string, uint16_t>> servers;
  for (const auto& r : mKnownRooms)
  {
    std::pair<std::string, uint16_t> key{r.address, r.port};
    if (std::ranges::find(servers, key) == servers.end())
      servers.push_back(std::move(key));
  }
  return servers;
}

void WebViewModel::NotifyServerStatus(const std::string& address, uint16_t port,
                                      const ServerStatus& status)
{
  {
    std::lock_guard lock(mServerStatusMutex);
    mServerStatuses[{address, port}] = status;
  }
  onServerStatusChanged.emit(address, port, status);
}

std::string WebViewModel::GetServerStatusesJson() const
{
  std::lock_guard lock(mServerStatusMutex);
  std::string json = "[";
  for (const auto& [key, status] : mServerStatuses)
  {
    if (json.size() > 1)
      json += ",";
    json += BuildServerStatusJson(key.first, key.second, status);
  }
  json += "]";
  return json;
}

// static
std::string WebViewModel::BuildServerStatusJson(const std::string& address, uint16_t port,
                                                const ServerStatus& status)
{
  std::string rooms = "[";
  for (std::size_t i = 0; i < status.rooms.size(); ++i)
  {
    if (i > 0)
      rooms += ",";
    rooms += std::format(R"({{"name":"{}","userCount":{},"maxUsers":{},"hasPassword":{}}})",
                         JsUtils::EscapeString(status.rooms[i].name),
                         status.rooms[i].userCount,
                         status.rooms[i].maxUsers,
                         status.rooms[i].hasPassword ? "true" : "false");
  }
  rooms += "]";

  return std::format(
    R"({{"address":"{}","port":{},"online":{},"serverVersion":"{}","rooms":{}}})",
    JsUtils::EscapeString(address),
    port,
    status.online ? "true" : "false",
    JsUtils::EscapeString(status.serverVersion),
    rooms);
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
      R"({{"address":"{}","port":{},"roomName":"{}"}})",
      JsUtils::EscapeString(rooms[i].address),
      rooms[i].port,
      JsUtils::EscapeString(rooms[i].roomName));
  }
  json += "]";
  return json;
}
