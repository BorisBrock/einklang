#pragma once

class EnetClient;
class WebViewModel;
class ConfigManager;

class RoomEntryConnector
{
public:
  static void Connect(EnetClient& enetClient, WebViewModel& viewModel, ConfigManager& configManager);
};
