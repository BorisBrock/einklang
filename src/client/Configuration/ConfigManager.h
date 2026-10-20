#pragma once

#include <string>

#include "ClientConfig.h"

class ConfigManager
{
public:
  explicit ConfigManager(std::string filename);

  bool Load();
  bool Save() const;

  ClientConfig& Get();
  const ClientConfig& Get() const;

private:
  std::string mFilename;
  ClientConfig mConfig{};
};
