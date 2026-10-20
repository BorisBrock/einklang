#pragma once

class WebViewModel;
class EnetClient;

class ChatConnector
{
public:
  static void Connect(WebViewModel& viewModel, EnetClient& enetClient);
};
