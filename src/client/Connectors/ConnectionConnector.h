#pragma once

class WebViewModel;
class EnetClient;
class ConfigManager;

class ConnectionConnector
{
public:
  static void Connect(WebViewModel& viewModel, EnetClient& enetClient, ConfigManager& configManager);
};
