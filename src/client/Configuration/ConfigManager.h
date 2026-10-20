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

  // Best-effort name of the logged-in OS user (full name if available,
  // otherwise the login name). Returns "User" if nothing can be determined.
  static std::string GetOsUserName();

private:
  std::string mFilename;
  ClientConfig mConfig{};
};
