#include <print>
#include <filesystem>
#include <system_error>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <string>
#include <stdint.h>
#include "../common/Version.h"
#include "Networking/EnetServer.h"
#include "Configuration/ConfigManager.h"
#include "Rooms/ChatRoomManager.h"
#include "Connectors/LoginConnector.h"
#include "Connectors/ChatConnector.h"
#include "Connectors/VoiceConnector.h"

// Atomic flag for thread-safe signal handling
std::atomic<bool> running{true};

void signalHandler(int signal)
{
  if (signal == SIGINT || signal == SIGTERM)
  {
    running = false;
  }
}

namespace
{
std::filesystem::path ResolveDataDir()
{
  const std::filesystem::path dockerDataDir{"/data"};
  if (std::filesystem::exists(dockerDataDir))
  {
    return dockerDataDir;
  }

  return std::filesystem::current_path();
}
}

int main()
{
  // Disable stdout buffering so log lines appear immediately in container runtimes
  // (e.g. Portainer/Docker) that don't allocate a TTY.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // Register signal handlers
  std::signal(SIGINT, signalHandler);  // CTRL+C
  std::signal(SIGTERM, signalHandler); // Docker stop / kill

#ifdef _WIN32
  std::signal(SIGBREAK, signalHandler); // Windows CTRL+Break
#endif

  std::println("Einklang Server v{}", EINKLANG_VERSION_STRING);

  const auto dataDir = ResolveDataDir();
  std::error_code dirError;
  std::filesystem::create_directories(dataDir, dirError);
  if (dirError)
  {
    std::println("[IO] Failed to create data directory '{}': {}", dataDir.string(), dirError.message());
    return EXIT_FAILURE;
  }

  std::println("Using data directory: {}", dataDir.string());

  const auto configPath = dataDir / "server-config.yml";
  std::println("Reading config from '{}'...", configPath.string());
  ConfigManager configManager(configPath.string());
  if (!configManager.Load())
  {
    std::println("Failed to load config.");
    return EXIT_FAILURE;
  }
  const ServerConfig& config = configManager.Get();

  std::println("Creating chat rooms...");
  ChatRoomManager chatRoomManager;
  chatRoomManager.InitializeFromConfig(config);
  std::println("Created {} chat room(s).", chatRoomManager.Rooms().size());

  const auto dbPath = dataDir / "einklang.db";
  std::println("Opening message database at '{}'...", dbPath.string());
  chatRoomManager.OpenDatabase(dbPath.string());

  std::println("Starting server on port {}...", config.port);
  EnetServer server;
  server.PacketHandler().SetLimits(config.maxMessageLength, config.maxUserNameLength);
  LoginConnector::Connect(server.PacketHandler(), chatRoomManager);
  ChatConnector::Connect(server.PacketHandler(), chatRoomManager);
  VoiceConnector::Connect(server.PacketHandler(), chatRoomManager);
  if (!server.Start(config.port))
  {
    std::println("Failed to start server.");
    return EXIT_FAILURE;
  }

  while (running)
  {
    server.ProcessEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::println("Stopping server...");
  server.Stop();

  return EXIT_SUCCESS;
}