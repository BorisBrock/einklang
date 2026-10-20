#pragma once

#include <functional>
#include <string>

class WebViewModel;
class ConfigManager;

// Pure saucer bridge: owns the window/webview, subscribes to WebViewModel
// events and translates them into JS execute() calls, and forwards incoming
// JS function calls back into the WebViewModel.
class WebUI
{
public:
  WebUI(WebViewModel& viewModel, ConfigManager& configManager);

  int Run();

private:
  WebViewModel&  mViewModel;
  ConfigManager& mConfigManager;
  std::function<void(const std::string&)> mExecuteJs;
};