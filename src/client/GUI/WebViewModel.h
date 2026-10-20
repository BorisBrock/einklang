#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../../common/Event.h"
#include "../../common/ChatMessage.h"
#include "../../common/ServerStatus.h"
#include "../Configuration/ClientConfig.h"

// Holds all GUI-relevant state and acts as the hub between the JavaScript
// front-end and the rest of the C++ application.
//
// Data flow
//   JS → WebUI bridge → WebViewModel::Handle*()
//        → emits onChatMessageSubmitted / onConnectToServer
//        → Connectors react
//
//   C++ (Connectors) → WebViewModel::AddKnownRoom / NotifyRoomEntryResponse
//        → emits onRoomAdded / onRoomEntryResult
//        → WebUI bridge translates to execute(js)
class WebViewModel
{
public:
  // --- Called from client.cpp / Connectors (C++ → VM) ---

  // Seed the room list before the GUI starts (call before WebUI::Run())
  void SetKnownRooms(std::vector<ClientConfig::KnownRoom> rooms);

  // Add a single room (deduplicates); fires onRoomAdded if the room is new
  void AddKnownRoom(const ClientConfig::KnownRoom& room);

  // Push a room-entry result toward the GUI; fires onRoomEntryResult
  void NotifyRoomEntryResponse(bool success, const std::string& errorMessage);

  // Mark a room as the currently active room; fires onActiveRoomChanged
  void SetActiveRoom(const std::string& address, uint16_t port, const std::string& roomName);

  // Notify the GUI of an incoming chat message; fires onNewChatMessage
  void NotifyNewChatMessage(const std::string& userName, const std::string& text);

  // Notify the GUI of an incoming chat image; fires onNewChatImage
  void NotifyNewChatImage(const std::string& userName, std::vector<std::uint8_t> imageData);

  // Notify the GUI of an updated user list; fires onRoomUsersUpdate
  void NotifyRoomUsersUpdate(std::vector<std::string> userNames);

  // Notify the GUI that a user is currently speaking; fires onUserSpeaking
  void NotifyUserSpeaking(const std::string& userName, bool isSpeaking);

  // Notify the GUI of the room's recent chat history; fires onRoomChatHistory
  void NotifyRoomChatHistory(std::vector<ChatMessage> entries);

  // Returns the known rooms serialised as a JSON array (used by WebUI for get_known_rooms)
  std::string GetKnownRoomsJson() const;

  // Returns the distinct (address, port) pairs of all known rooms
  std::vector<std::pair<std::string, uint16_t>> GetKnownServers() const;

  // Store the latest status of a server; fires onServerStatusChanged.
  // Safe to call from any thread.
  void NotifyServerStatus(const std::string& address, uint16_t port, const ServerStatus& status);

  // Returns the latest status of every known server as a JSON array of the
  // objects produced by BuildServerStatusJson (used by WebUI for get_server_statuses)
  std::string GetServerStatusesJson() const;

  // Serialises one server's status as a JSON object:
  // {"address","port","online","serverVersion","rooms":[{"name","userCount","maxUsers"}]}
  static std::string BuildServerStatusJson(const std::string& address, uint16_t port,
                                           const ServerStatus& status);

  // Returns the username the local user connected with (empty before first login)
  const std::string& GetLocalUserName() const { return mLocalUserName; }

  // --- Called from the WebUI bridge (JS → VM) ---

  void HandleChatMessage(std::string msg);
  void HandleChatImage(std::vector<std::uint8_t> imageData);
  void HandleConnectToServer(std::string server, uint16_t port, std::string room,
                             std::string password, std::string username);

  // Called when the user clicks the mute/unmute button in the GUI.
  void HandleVoiceMuteToggle(bool muted);

  // Called when the settings dialog applies new voice settings.
  // Args: vadThreshold, inputGain
  void HandleSetVoiceSettings(float vadThreshold, float inputGain);

  // Register a callable that returns the current microphone input RMS.
  // Set from client.cpp after the VoicePipeline is created so WebUI can
  // poll it via the get_input_rms bridge without coupling WebUI to the pipeline.
  void  SetInputRmsGetter(std::function<float()> fn);
  float GetInputRms() const;

  // Called when the user clicks a known room in the list to switch into it.
  // address/port identify the server; userName is the previously-used name.
  void HandleSwitchToRoom(std::string address, uint16_t port,
                          std::string roomName, std::string userName);

  // Remove a single known room from the model & config; disconnects if active.
  void HandleRemoveRoom(std::string address, uint16_t port, std::string roomName);

  // Remove all known rooms for a server; disconnects if the active room is on it.
  void HandleRemoveServer(std::string address, uint16_t port);

  // Change address/port/user name of all known rooms on a server; disconnects
  // if the active room is on it. Returns the updated room list as JSON.
  std::string HandleEditServer(const std::string& oldAddress, uint16_t oldPort,
                               const std::string& newAddress, uint16_t newPort,
                               const std::string& userName);

  // Applies a server edit to a room list (shared by the view-model and the
  // persisted config). Rooms that would collide with an existing room on the
  // target server are dropped. Returns true if anything changed.
  static bool EditServerRooms(std::vector<ClientConfig::KnownRoom>& rooms,
                              const std::string& oldAddress, uint16_t oldPort,
                              const std::string& newAddress, uint16_t newPort,
                              const std::string& userName);

  // --- Events: VM → C++  (Connectors subscribe to these) ---

  Event<std::string>                                        onChatMessageSubmitted;
  Event<std::vector<std::uint8_t>>                          onChatImageSubmitted;
  Event<std::string, uint16_t, std::string, std::string, std::string> onConnectToServer;

  // Fired when the user asks to switch to a known room from the list.
  // Args: address, port, roomName, userName
  Event<std::string, uint16_t, std::string, std::string>   onSwitchToRoom;

  // Fired when the active room is removed so the connector can disconnect.
  Event<>                                                  onActiveRoomLeft;

  // Fired when the user clicks the microphone mute/unmute button.
  // Args: muted (true = mic off)
  Event<bool>                                              onVoiceMuteToggled;

  // Fired when the settings dialog applies new voice settings.
  // Args: vadThreshold, inputGain
  Event<float, float>                                      onVoiceSettingsChanged;

  // Fired whenever the set of known rooms changes (added, removed or edited).
  Event<>                                                  onKnownRoomsChanged;

  // --- Events: VM → GUI  (WebUI subscribes to these) ---

  Event<ClientConfig::KnownRoom> onRoomAdded;
  Event<bool, std::string> onRoomEntryResult;

  // Fired when the active room changes. Args: address, port, roomName
  Event<std::string, uint16_t, std::string>                onActiveRoomChanged;

  // Fired when the server delivers a new chat message. Args: userName, text
  Event<std::string, std::string>                          onNewChatMessage;

  // Fired when the server delivers a new chat image. Args: userName, raw bytes
  Event<std::string, std::vector<std::uint8_t>>            onNewChatImage;

  // Fired when the room user list is updated. Args: list of user names
  Event<std::vector<std::string>>     onRoomUsersUpdate;

  // Fired when a server's status changes. Args: address, port, status
  Event<std::string, uint16_t, ServerStatus> onServerStatusChanged;

  // Fired when voice activity is detected for a user. Args: userName, isSpeaking
  Event<std::string, bool>            onUserSpeaking;

  // Fired when the server delivers the room's chat history upon entry
  Event<std::vector<ChatMessage>> onRoomChatHistory;

  // Fired once from inside the WebView2 message loop immediately after the
  // window becomes visible.  Subscribers that need to call into WASAPI/COM
  // (e.g. VoicePipeline::Start) must wait for this event so that the COM
  // STA apartment established by saucer/WebView2 is already in place.
  Event<> onReady;

private:
  static std::string BuildRoomsJson(const std::vector<ClientConfig::KnownRoom>& rooms);

  std::vector<ClientConfig::KnownRoom> mKnownRooms;

  // The username the local user last connected with.
  std::string mLocalUserName;

  // Tracks the currently active room so removal can detect a conflict.
  std::string mActiveAddress;
  uint16_t    mActivePort{0};
  std::string mActiveRoom;

  // Latest status per server; written from the status polling thread.
  mutable std::mutex mServerStatusMutex;
  std::map<std::pair<std::string, uint16_t>, ServerStatus> mServerStatuses;

  // Optional getter for the microphone input RMS level.
  std::function<float()> mInputRmsGetter;
};
