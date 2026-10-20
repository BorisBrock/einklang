#include "ConfigManager.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <string>

#include <yaml-cpp/yaml.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <pwd.h>
#  include <unistd.h>
#  include <cstdlib>
#endif

ConfigManager::ConfigManager(std::string filename)
  : mFilename(std::move(filename))
{
}

bool ConfigManager::Load()
{
  // No config yet: start from the defaults; the GUI shows its welcome screen
  // and the file is written on the first Save().
  std::error_code ec;
  if (!std::filesystem::exists(mFilename, ec))
  {
    mFirstStart = true;
    return true;
  }

  try
  {
    YAML::Node node = YAML::LoadFile(mFilename);

    if (node["user-name"])
      mConfig.userName = node["user-name"].as<std::string>();

    if (node["last-used-server-address"])
      mConfig.lastUsedServerAddress = node["last-used-server-address"].as<std::string>();

    if (node["last-used-room-name"])
      mConfig.lastUsedRoomName = node["last-used-room-name"].as<std::string>();

    if (node["last-used-server-port"])
      mConfig.lastUsedServerPort = node["last-used-server-port"].as<uint16_t>();

    if (node["window-width"])
      mConfig.windowWidth = node["window-width"].as<int>();

    if (node["window-height"])
      mConfig.windowHeight = node["window-height"].as<int>();

    if (node["theme"])
      mConfig.theme = node["theme"].as<std::string>();

    // Voice settings
    if (node["voice-enabled"])
      mConfig.voiceEnabled = node["voice-enabled"].as<bool>();
    if (node["input-gain"])
      mConfig.inputGain = node["input-gain"].as<float>();
    if (node["output-volume"])
      mConfig.outputVolume = node["output-volume"].as<float>();
    if (node["input-device"])
      mConfig.inputDevice = node["input-device"].as<std::string>();
    if (node["output-device"])
      mConfig.outputDevice = node["output-device"].as<std::string>();
    if (node["vad-snr-factor"])
      mConfig.vadSnrFactor = node["vad-snr-factor"].as<float>();
    if (node["vad-attack-frames"])
      mConfig.vadAttackFrames = node["vad-attack-frames"].as<int>();
    if (node["vad-release-frames"])
      mConfig.vadReleaseFrames = node["vad-release-frames"].as<int>();
    if (node["vad-zcr-min"])
      mConfig.vadZcrMin = node["vad-zcr-min"].as<float>();
    if (node["vad-zcr-max"])
      mConfig.vadZcrMax = node["vad-zcr-max"].as<float>();
    if (node["vad-min-rms-floor"])
      mConfig.vadMinRmsFloor = node["vad-min-rms-floor"].as<float>();
    else if (node["vad-threshold"])  // backward-compat: older config files used vad-threshold
      mConfig.vadMinRmsFloor = node["vad-threshold"].as<float>();
    if (node["noise-filter"])
      mConfig.noiseFilter = node["noise-filter"].as<bool>();

    // Written as "known-rooms: ~" while the list is empty
    if (node["known-rooms"] && node["known-rooms"].IsSequence())
    {
      for (const auto& roomNode : node["known-rooms"])
      {
        ClientConfig::KnownRoom room;
        room.address    = roomNode["address"].as<std::string>();
        room.port       = roomNode["port"].as<uint16_t>();
        room.roomName   = roomNode["room-name"].as<std::string>();
        if (roomNode["password"])
          room.password = roomNode["password"].as<std::string>();
        mConfig.knownRooms.push_back(room);
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
    root["user-name"]                = mConfig.userName;
    root["last-used-server-address"] = mConfig.lastUsedServerAddress;
    root["last-used-room-name"]      = mConfig.lastUsedRoomName;
    root["last-used-server-port"]    = mConfig.lastUsedServerPort;
    root["window-width"]             = mConfig.windowWidth;
    root["window-height"]            = mConfig.windowHeight;
    root["theme"]                    = mConfig.theme;
    root["voice-enabled"]            = mConfig.voiceEnabled;
    root["input-gain"]               = mConfig.inputGain;
    root["output-volume"]            = mConfig.outputVolume;
    root["input-device"]             = mConfig.inputDevice;
    root["output-device"]            = mConfig.outputDevice;
    root["vad-snr-factor"]           = mConfig.vadSnrFactor;
    root["vad-attack-frames"]        = mConfig.vadAttackFrames;
    root["vad-release-frames"]       = mConfig.vadReleaseFrames;
    root["vad-zcr-min"]              = mConfig.vadZcrMin;
    root["vad-zcr-max"]              = mConfig.vadZcrMax;
    root["vad-min-rms-floor"]        = mConfig.vadMinRmsFloor;
    root["noise-filter"]             = mConfig.noiseFilter;

    YAML::Node rooms;
    for (const auto& room : mConfig.knownRooms)
    {
      YAML::Node roomNode;
      roomNode["address"]     = room.address;
      roomNode["port"]        = room.port;
      roomNode["room-name"]   = room.roomName;
      roomNode["password"]    = room.password;
      rooms.push_back(roomNode);
    }
    root["known-rooms"] = rooms;

    // The config directory may not exist yet on first start
    const std::filesystem::path parent = std::filesystem::path(mFilename).parent_path();
    if (!parent.empty())
      std::filesystem::create_directories(parent);

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

bool ConfigManager::IsFirstStart() const
{
  return mFirstStart;
}

ClientConfig& ConfigManager::Get()
{
  return mConfig;
}

const ClientConfig& ConfigManager::Get() const
{
  return mConfig;
}

#ifdef _WIN32
static std::string WideToUtf8(const wchar_t* wide)
{
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1)
    return {};
  std::string result(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), size, nullptr, nullptr);
  return result;
}
#endif

// static
std::string ConfigManager::GetOsUserName()
{
  std::string name;

#ifdef _WIN32
  wchar_t buffer[257]{};
  DWORD length = static_cast<DWORD>(std::size(buffer));
  if (GetUserNameW(buffer, &length))
    name = WideToUtf8(buffer);
#else
  if (const passwd* pw = getpwuid(getuid()))
  {
    // The GECOS field holds the full name, optionally followed by
    // comma-separated extras (room, phone, ...).
    if (pw->pw_gecos != nullptr)
    {
      name = pw->pw_gecos;
      name = name.substr(0, name.find(','));
    }
    if (name.empty() && pw->pw_name != nullptr)
      name = pw->pw_name;
  }
  if (name.empty())
  {
    if (const char* user = std::getenv("USER"))
      name = user;
  }
#endif

  // Must fit the server's default user name limit; don't split a UTF-8 sequence
  if (name.size() > 64)
  {
    std::size_t cut = 64;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80)
      --cut;
    name.resize(cut);
  }

  return name.empty() ? "User" : name;
}
