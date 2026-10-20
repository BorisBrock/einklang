#include "ServerStatusConnector.h"

#include "../GUI/WebViewModel.h"
#include "../Networking/ServerStatusPoller.h"

void ServerStatusConnector::Connect(WebViewModel& viewModel, ServerStatusPoller& poller)
{
  // Keep the polled server set in sync with the known rooms
  poller.SetServers(viewModel.GetKnownServers());
  viewModel.onKnownRoomsChanged.subscribe([&viewModel, &poller]()
  {
    poller.SetServers(viewModel.GetKnownServers());
  });

  // Entering a room changes occupancy (also of the room just left) — refresh now
  viewModel.onActiveRoomChanged.subscribe([&poller](const std::string&, uint16_t, const std::string&)
  {
    poller.RequestRefresh();
  });

  // Forward poll results to the GUI
  poller.onServerStatusChanged.subscribe(
    [&viewModel](std::string address, uint16_t port, ServerStatus status)
    {
      viewModel.NotifyServerStatus(address, port, status);
    });
}
