#include <print>
#include <filesystem>
#include <system_error>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <string>
#include <string_view>
#include <stdint.h>
#include "Networking/EnetServer.h"
#include "Networking/ServerPacketFactory.h"
#include "Networking/StatusProbe.h"
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

// Connections that may exist on top of the room capacity: clients that are
// connected but not in a room yet, or whose previous connection has not
// timed out yet.
constexpr std::size_t kConnectionHeadroom = 16;

std::size_t ResolveMaxConnections(const ServerConfig& config)
{
  if (config.maxConnections > 0)
    return config.maxConnections;

  std::size_t capacity = 0;
  for (const auto& room : config.rooms)
    capacity += room.maxUsers;
  return capacity + kConnectionHeadroom;
}
}

// `--healthcheck`: exit 0 if the server configured in the data directory
// answers a status query on localhost, 1 otherwise (Docker HEALTHCHECK).
int RunHealthcheck()
{
  ConfigManager configManager((ResolveDataDir() / "server-config.yml").string());
  if (!configManager.Load())
    return EXIT_FAILURE;

  const auto port = configManager.Get().port;
  if (!StatusProbe::ProbeLocalServer(port, 3000))
  {
    std::println("Unhealthy: no status reply on UDP port {}.", port);
    return EXIT_FAILURE;
  }

  std::println("Healthy: server answers on UDP port {}.", port);
  return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
  for (int i = 1; i < argc; ++i)
  {
    if (std::string_view(argv[i]) == "--healthcheck")
      return RunHealthcheck();
  }

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
  server.SetStatusProvider([&chatRoomManager]()
  {
    return ServerPacketFactory::BuildServerStatus(chatRoomManager.Rooms());
  });
  if (!server.Start(config.port, ResolveMaxConnections(config)))
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