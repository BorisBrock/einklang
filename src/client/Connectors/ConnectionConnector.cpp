#include <algorithm>
#include <mutex>
#include <print>
#include <thread>
#include "../Configuration/ConfigManager.h"
#include "../GUI/WebViewModel.h"
#include "../Networking/ClientPacketFactory.h"
#include "../Networking/EnetClient.h"
#include "ConnectionConnector.h"

// Serialises connect/disconnect work, which runs on detached threads.
static std::mutex sConnectionMutex;

static void ConnectAndEnterRoom(WebViewModel& viewModel, EnetClient& enetClient,
                                const std::string& server, uint16_t port,
                                const std::string& room, const std::string& password,
                                const std::string& username)
{
  std::lock_guard lock(sConnectionMutex);

  // Only reconnect if we're not already connected to this server
  if (!enetClient.IsConnectedTo(server, port))
  {
    enetClient.Disconnect();
    if (!enetClient.Connect(server, port))
    {
      std::println("Failed to connect to server {}:{}.", server, port);
      viewModel.NotifyRoomEntryResponse(false, "Could not reach the server. Please check the address and port.");
      return;
    }
  }

  const auto packet = ClientPacketFactory::RoomEntryRequest(room, password, username);
  if (!enetClient.SendPacket(packet))
  {
    std::println("Failed to send room entry request packet.");
    viewModel.NotifyRoomEntryResponse(false, "Could not reach the server. Please check the address and port.");
  }
}

void ConnectionConnector::Connect(WebViewModel& viewModel, EnetClient& enetClient,
                                  ConfigManager& configManager)
{
  // Dialog-based connection
  viewModel.onConnectToServer.subscribe([&](std::string server, uint16_t port, std::string room,
                                            std::string password, std::string username)
  {
    std::println("Connect to server requested: server = {}:{}, room = {}, password = {}, username = {}",
                 server, port, room, password, username);
    std::thread([&viewModel, &enetClient, server, port, room, password, username]() {
      ConnectAndEnterRoom(viewModel, enetClient, server, port, room, password, username);
    }).detach();
  });

  // Room-list click: switch to a known room (may reuse existing connection)
  viewModel.onSwitchToRoom.subscribe([&](std::string address, uint16_t port,
                                         std::string roomName, std::string userName)
  {
    // Look up the stored password for this room from config
    std::string password;
    const ClientConfig& cfg = configManager.Get();
    auto it = std::ranges::find_if(cfg.knownRooms, [&](const ClientConfig::KnownRoom& r)
    {
      return r.address == address && r.port == port && r.roomName == roomName;
    });
    if (it != cfg.knownRooms.end())
      password = it->password;

    std::println("Switch to room requested: server = {}:{}, room = {}, userName = {}",
                 address, port, roomName, userName);
    std::thread([&viewModel, &enetClient, address, port, roomName, password, userName]() {
      ConnectAndEnterRoom(viewModel, enetClient, address, port, roomName, password, userName);
    }).detach();
  });

  // When the active room is removed by the user, disconnect from the server.
  // Run in a thread to avoid blocking the webview callback (Disconnect polls
  // for up to 500 ms waiting for the server's acknowledgement).
  viewModel.onActiveRoomLeft.subscribe([&enetClient]()
  {
    std::println("Active room removed — disconnecting from server.");
    std::thread([&enetClient]()
    {
      std::lock_guard lock(sConnectionMutex);
      enetClient.Disconnect();
    }).detach();
  });
}
