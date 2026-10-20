#pragma once

#include <string>

#include "ServerConfig.h"

class ConfigManager
{
public:
  explicit ConfigManager(std::string filename);

  bool Load();
  bool Save() const;

  ServerConfig& Get();
  const ServerConfig& Get() const;

private:
  std::string mFilename;
  ServerConfig mConfig{};
};
