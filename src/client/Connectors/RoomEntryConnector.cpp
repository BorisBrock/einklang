#include "RoomEntryConnector.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <print>
#include <string>

#include "../Configuration/ConfigManager.h"
#include "../GUI/WebViewModel.h"
#include "../Networking/EnetClient.h"

void RoomEntryConnector::Connect(EnetClient& enetClient, WebViewModel& viewModel, ConfigManager& configManager)
{
  // Cache the pending connection details so they are available when the
  // server responds. Wrapped in a shared_ptr so both lambda captures share it.
  struct PendingConnection
  {
    std::string server;
    std::string room;
    std::string userName;
    std::string password;
    uint16_t    port{24816};
  };
  auto pending = std::make_shared<PendingConnection>();

  // Dialog-based connection
  viewModel.onConnectToServer.subscribe(
    [pending](std::string server, uint16_t port, std::string room, std::string password, std::string userName)
    {
      pending->server   = std::move(server);
      pending->room     = std::move(room);
      pending->userName = std::move(userName);
      pending->password = std::move(password);
      pending->port     = port;
    });

  // Room-list click switch (password is looked up from config in ConnectionConnector
  // and also stored here so it can be re-saved on success)
  viewModel.onSwitchToRoom.subscribe(
    [pending, &configManager](std::string address, uint16_t port, std::string roomName, std::string userName)
    {
      pending->server   = std::move(address);
      pending->room     = std::move(roomName);
      pending->userName = std::move(userName);
      pending->port     = port;

      // Look up stored password for this room
      const ClientConfig& cfg = configManager.Get();
      auto it = std::ranges::find_if(cfg.knownRooms, [&](const ClientConfig::KnownRoom& r)
      {
        return r.address == pending->server &&
               r.port    == pending->port    &&
               r.roomName == pending->room;
      });
      pending->password = (it != cfg.knownRooms.end()) ? it->password : std::string{};
    });

  enetClient.PacketHandler().onRoomEntryResponse.subscribe(
    [&viewModel, &configManager, pending](bool success, std::string errorMessage)
    {
      if (success)
      {
        std::println("Room entry granted.");

        // Update last-used fields
        ClientConfig& config = configManager.Get();
        config.lastUsedServerAddress = pending->server;
        config.lastUsedRoomName      = pending->room;
        config.lastUsedUserName      = pending->userName;
        config.lastUsedServerPort    = pending->port;

        // Add or update the matching known-room entry
        auto it = std::ranges::find_if(config.knownRooms, [&](const ClientConfig::KnownRoom& r)
        {
          return r.address == pending->server &&
                 r.port    == pending->port    &&
                 r.roomName == pending->room;
        });

        if (it != config.knownRooms.end())
        {
          it->userName = pending->userName;
          it->password = pending->password;
        }
        else
        {
          config.knownRooms.push_back({pending->server, pending->port,
                                       pending->room, pending->userName,
                                       pending->password});
          viewModel.AddKnownRoom({pending->server, pending->port,
                                  pending->room, pending->userName,
                                  pending->password});
        }

        if (!configManager.Save())
        {
          std::println("Warning: failed to save config after successful room entry.");
        }

        // Highlight the newly active room in the GUI
        viewModel.SetActiveRoom(pending->server, pending->port, pending->room);
      }
      else
      {
        std::println("Room entry denied: {}", errorMessage);
      }

      viewModel.NotifyRoomEntryResponse(success, errorMessage);
    });
}

