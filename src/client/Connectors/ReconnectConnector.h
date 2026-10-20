#pragma once

class WebViewModel;
class EnetClient;

// Re-enters the active room after the server connection drops, retrying with
// increasing delays until it succeeds or the user leaves/switches the room.
class ReconnectConnector
{
public:
  static void Connect(WebViewModel& viewModel, EnetClient& enetClient);
};
