#include "ConfigManager.h"

#include <cstdint>
#include <fstream>
#include <print>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

ConfigManager::ConfigManager(std::string filename)
  : mFilename(std::move(filename))
{
}

bool ConfigManager::Load()
{
  try
  {
    YAML::Node node = YAML::LoadFile(mFilename);

    if (node["port"])
      mConfig.port = node["port"].as<uint16_t>();

    if (node["maxHistoryMessages"])
      mConfig.maxHistoryMessages = node["maxHistoryMessages"].as<uint32_t>();
    if (node["maxMessageLength"])
      mConfig.maxMessageLength = node["maxMessageLength"].as<uint32_t>();
    if (node["maxUserNameLength"])
      mConfig.maxUserNameLength = node["maxUserNameLength"].as<uint32_t>();

    if (node["voiceEnabled"])
      mConfig.voiceEnabled = node["voiceEnabled"].as<bool>();

    if (node["rooms"])
    {
      for (const auto& roomNode : node["rooms"])
      {
        ServerConfig::RoomConfig room;
        room.name       = roomNode["name"].as<std::string>();
        room.maxUsers   = roomNode["maxUsers"].as<uint16_t>();
        room.password   = roomNode["password"].as<std::string>();
        if (roomNode["voiceEnabled"])
          room.voiceEnabled = roomNode["voiceEnabled"].as<bool>();
        else
          room.voiceEnabled = mConfig.voiceEnabled; // inherit global default
        mConfig.rooms.push_back(room);
      }
    }
  }
  catch (const std::exception& e)
  {
    std::println("Error loading config '{}': {}", mFilename, e.what());
    return false;
  }

  return true;
}

bool ConfigManager::Save() const
{
  try
  {
    YAML::Node root;
    root["port"] = mConfig.port;
    root["voiceEnabled"] = mConfig.voiceEnabled;

    YAML::Node rooms;
    for (const auto& room : mConfig.rooms)
    {
      YAML::Node roomNode;
      roomNode["name"]       = room.name;
      roomNode["maxUsers"]   = room.maxUsers;
      roomNode["password"]   = room.password;
      roomNode["voiceEnabled"] = room.voiceEnabled;
      rooms.push_back(roomNode);
    }
    root["rooms"] = rooms;

    std::ofstream fout(mFilename);
    if (!fout.is_open())
    {
      std::println("Error saving config: cannot open '{}' for writing.", mFilename);
      return false;
    }
    fout << root;
  }
  catch (const std::exception& e)
  {
    std::println("Error saving config '{}': {}", mFilename, e.what());
    return false;
  }

  return true;
}

ServerConfig& ConfigManager::Get()
{
  return mConfig;
}

const ServerConfig& ConfigManager::Get() const
{
  return mConfig;
}
