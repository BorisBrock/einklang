#pragma once

class WebViewModel;
class ServerStatusPoller;

class ServerStatusConnector
{
public:
  static void Connect(WebViewModel& viewModel, ServerStatusPoller& poller);
};
