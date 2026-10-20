#include <doctest.h>

#include <filesystem>
#include <string>

#include "../../src/client/Configuration/ConfigManager.h"
#include "TestUtils.h"

using TestUtils::TempDir;

TEST_SUITE("ClientConfig")
{
TEST_CASE("a missing file is a first start with default settings")
{
  TempDir dir;
  ConfigManager manager(dir.File("client-config.yml"));
  REQUIRE(manager.Load());
  CHECK(manager.IsFirstStart());

  const auto& config = manager.Get();
  CHECK(config.userName.empty());
  CHECK(config.lastUsedServerAddress == "localhost");
  CHECK(config.lastUsedServerPort == 24816);
  CHECK(config.knownRooms.empty());
  CHECK(config.windowWidth == 1280);
  CHECK(config.windowHeight == 800);
  CHECK_FALSE(config.windowMaximized);
  CHECK(config.theme == "dark");
  CHECK(config.voiceEnabled);
  CHECK(config.inputGain == doctest::Approx(1.0f));
  CHECK(config.outputVolume == doctest::Approx(1.0f));
  CHECK(config.inputDevice.empty());
  CHECK(config.outputDevice.empty());
  CHECK(config.noiseFilter);

  // Loading must not create the file; that is Save()'s job.
  CHECK_FALSE(std::filesystem::exists(dir.Path() / "client-config.yml"));
}

TEST_CASE("every key is read")
{
  TempDir dir;
  const auto path = dir.Write("client-config.yml", R"(
user-name: "Jörg Müller"
last-used-server-address: chat.example.org
last-used-room-name: Lobby
last-used-server-port: 4000
window-width: 1280
window-height: 720
window-maximized: true
theme: nord
voice-enabled: false
input-gain: 1.5
output-volume: 0.25
input-device: "USB Mic"
output-device: "Headphones"
vad-snr-factor: 6
vad-attack-frames: 3
vad-release-frames: 30
vad-zcr-min: 400
vad-zcr-max: 6000
vad-min-rms-floor: 0.01
noise-filter: false
known-rooms:
  - address: 192.168.1.20
    port: 24816
    room-name: Lobby
    password: secret
  - address: "::1"
    port: 5000
    room-name: "Game Room 1"
)");

  ConfigManager manager(path);
  REQUIRE(manager.Load());
  CHECK_FALSE(manager.IsFirstStart());

  const auto& c = manager.Get();
  CHECK(c.userName == "Jörg Müller");
  CHECK(c.lastUsedServerAddress == "chat.example.org");
  CHECK(c.lastUsedRoomName == "Lobby");
  CHECK(c.lastUsedServerPort == 4000);
  CHECK(c.windowWidth == 1280);
  CHECK(c.windowHeight == 720);
  CHECK(c.windowMaximized);
  CHECK(c.theme == "nord");
  CHECK_FALSE(c.voiceEnabled);
  CHECK(c.inputGain == doctest::Approx(1.5f));
  CHECK(c.outputVolume == doctest::Approx(0.25f));
  CHECK(c.inputDevice == "USB Mic");
  CHECK(c.outputDevice == "Headphones");
  CHECK(c.vadSnrFactor == doctest::Approx(6.0f));
  CHECK(c.vadAttackFrames == 3);
  CHECK(c.vadReleaseFrames == 30);
  CHECK(c.vadZcrMin == doctest::Approx(400.0f));
  CHECK(c.vadZcrMax == doctest::Approx(6000.0f));
  CHECK(c.vadMinRmsFloor == doctest::Approx(0.01f));
  CHECK_FALSE(c.noiseFilter);

  REQUIRE(c.knownRooms.size() == 2);
  CHECK(c.knownRooms[0].address == "192.168.1.20");
  CHECK(c.knownRooms[0].port == 24816);
  CHECK(c.knownRooms[0].roomName == "Lobby");
  CHECK(c.knownRooms[0].password == "secret");
  CHECK(c.knownRooms[1].address == "::1");
  CHECK(c.knownRooms[1].port == 5000);
  CHECK(c.knownRooms[1].roomName == "Game Room 1");
  CHECK(c.knownRooms[1].password.empty()); // optional
}

TEST_CASE("omitted keys keep their defaults")
{
  TempDir dir;
  ConfigManager manager(dir.Write("client-config.yml", "theme: light\n"));
  REQUIRE(manager.Load());
  CHECK(manager.Get().theme == "light");
  CHECK(manager.Get().windowWidth == 1280);
  CHECK(manager.Get().vadReleaseFrames == 20);
  CHECK(manager.Get().knownRooms.empty());
}

TEST_CASE("a degenerate window size falls back to the default")
{
  TempDir dir;
  ConfigManager manager(dir.Write("client-config.yml", "window-width: 0\nwindow-height: 0\n"));
  REQUIRE(manager.Load());
  CHECK(manager.Get().windowWidth == 1280);
  CHECK(manager.Get().windowHeight == 800);
}

TEST_CASE("the legacy vad-threshold key is still understood")
{
  TempDir dir;

  SUBCASE("used when vad-min-rms-floor is absent")
  {
    ConfigManager manager(dir.Write("client-config.yml", "vad-threshold: 0.02\n"));
    REQUIRE(manager.Load());
    CHECK(manager.Get().vadMinRmsFloor == doctest::Approx(0.02f));
  }
  SUBCASE("ignored when vad-min-rms-floor is present")
  {
    ConfigManager manager(dir.Write("client-config.yml",
                                    "vad-threshold: 0.02\nvad-min-rms-floor: 0.03\n"));
    REQUIRE(manager.Load());
    CHECK(manager.Get().vadMinRmsFloor == doctest::Approx(0.03f));
  }
}

TEST_CASE("an empty known-rooms list written as ~ loads")
{
  TempDir dir;
  ConfigManager manager(dir.Write("client-config.yml", "known-rooms: ~\nuser-name: x\n"));
  REQUIRE(manager.Load());
  CHECK(manager.Get().knownRooms.empty());
  CHECK(manager.Get().userName == "x");
}

TEST_CASE("invalid files fail to load")
{
  TempDir dir;

  SUBCASE("malformed YAML")
  {
    ConfigManager manager(dir.Write("c.yml", "user-name: [unterminated\n"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("port out of range")
  {
    ConfigManager manager(dir.Write("c.yml", "last-used-server-port: 99999\n"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("known room without an address")
  {
    ConfigManager manager(dir.Write("c.yml", R"(
known-rooms:
  - port: 1
    room-name: Lobby
)"));
    CHECK_FALSE(manager.Load());
  }
  SUBCASE("non-numeric window size")
  {
    ConfigManager manager(dir.Write("c.yml", "window-width: wide\n"));
    CHECK_FALSE(manager.Load());
  }
}

TEST_CASE("everything saved loads back unchanged")
{
  TempDir dir;
  const auto path = dir.File("client-config.yml");

  ConfigManager writer(path);
  auto& c = writer.Get();
  c.userName = "Ünïcode \"quoted\" : name";
  c.lastUsedServerAddress = "10.0.0.1";
  c.lastUsedRoomName = "Room: with colon";
  c.lastUsedServerPort = 65535;
  c.windowWidth = 1920;
  c.windowHeight = 1080;
  c.windowMaximized = true;
  c.theme = "light";
  c.voiceEnabled = false;
  c.inputGain = 1.75f;
  c.outputVolume = 0.5f;
  c.inputDevice = "Mic #1";
  c.outputDevice = "Speakers (Realtek)";
  c.vadSnrFactor = 3.5f;
  c.vadAttackFrames = 4;
  c.vadReleaseFrames = 40;
  c.vadZcrMin = 300.0f;
  c.vadZcrMax = 7000.0f;
  c.vadMinRmsFloor = 0.002f;
  c.noiseFilter = false;
  c.knownRooms.push_back({.address = "example.org", .port = 1, .roomName = "A", .password = "p:w#"});
  c.knownRooms.push_back({.address = "fe80::1", .port = 24816, .roomName = "B", .password = ""});
  REQUIRE(writer.Save());

  ConfigManager reader(path);
  REQUIRE(reader.Load());
  CHECK_FALSE(reader.IsFirstStart());
  const auto& r = reader.Get();
  CHECK(r.userName == c.userName);
  CHECK(r.lastUsedServerAddress == c.lastUsedServerAddress);
  CHECK(r.lastUsedRoomName == c.lastUsedRoomName);
  CHECK(r.lastUsedServerPort == c.lastUsedServerPort);
  CHECK(r.windowWidth == c.windowWidth);
  CHECK(r.windowMaximized == c.windowMaximized);
  CHECK(r.windowHeight == c.windowHeight);
  CHECK(r.theme == c.theme);
  CHECK(r.voiceEnabled == c.voiceEnabled);
  CHECK(r.inputGain == doctest::Approx(c.inputGain));
  CHECK(r.outputVolume == doctest::Approx(c.outputVolume));
  CHECK(r.inputDevice == c.inputDevice);
  CHECK(r.outputDevice == c.outputDevice);
  CHECK(r.vadSnrFactor == doctest::Approx(c.vadSnrFactor));
  CHECK(r.vadAttackFrames == c.vadAttackFrames);
  CHECK(r.vadReleaseFrames == c.vadReleaseFrames);
  CHECK(r.vadZcrMin == doctest::Approx(c.vadZcrMin));
  CHECK(r.vadZcrMax == doctest::Approx(c.vadZcrMax));
  CHECK(r.vadMinRmsFloor == doctest::Approx(c.vadMinRmsFloor));
  CHECK(r.noiseFilter == c.noiseFilter);
  REQUIRE(r.knownRooms.size() == 2);
  for (std::size_t i = 0; i < 2; ++i)
  {
    CHECK(r.knownRooms[i].address == c.knownRooms[i].address);
    CHECK(r.knownRooms[i].port == c.knownRooms[i].port);
    CHECK(r.knownRooms[i].roomName == c.knownRooms[i].roomName);
    CHECK(r.knownRooms[i].password == c.knownRooms[i].password);
  }
}

TEST_CASE("a default config survives a save/load round trip")
{
  TempDir dir;
  const auto path = dir.File("client-config.yml");
  ConfigManager writer(path);
  REQUIRE(writer.Load());
  REQUIRE(writer.Save());

  ConfigManager reader(path);
  REQUIRE(reader.Load());
  CHECK_FALSE(reader.IsFirstStart());
  CHECK(reader.Get().knownRooms.empty());
  CHECK(reader.Get().theme == "dark");
}

TEST_CASE("saving creates missing parent directories")
{
  TempDir dir;
  const auto path = dir.File("nested/config/dir/client-config.yml");
  ConfigManager manager(path);
  manager.Get().userName = "nested";
  REQUIRE(manager.Save());

  ConfigManager reader(path);
  REQUIRE(reader.Load());
  CHECK(reader.Get().userName == "nested");
}

TEST_CASE("the OS user name is usable as a chat name")
{
  const auto name = ConfigManager::GetOsUserName();
  CHECK_FALSE(name.empty());
  CHECK(name.size() <= 64);
  CHECK(name.find(',') == std::string::npos); // GECOS extras are stripped
}
}
