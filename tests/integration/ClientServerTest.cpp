// End-to-end tests: a real EnetServer (with the same connectors as
// EinklangServerApp) and real EnetClients talk over UDP on localhost.
//
// The server binds port 0, so every test gets a free port from the OS and
// test runs never collide. The server runs its event loop on its own thread;
// assertions only look at what clients receive, never at server state.

#include <doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../src/client/Networking/ClientPacketFactory.h"
#include "../../src/client/Networking/EnetClient.h"
#include "../../src/client/Networking/ServerStatusPoller.h"
#include "../../src/common/VoicePacket.h"
#include "../../src/server/Configuration/ServerConfig.h"
#include "../../src/server/Connectors/ChatConnector.h"
#include "../../src/server/Connectors/LoginConnector.h"
#include "../../src/server/Connectors/VoiceConnector.h"
#include "../../src/server/Networking/EnetServer.h"
#include "../../src/server/Networking/ServerPacketFactory.h"
#include "../../src/server/Networking/StatusProbe.h"
#include "../../src/server/Rooms/ChatRoomManager.h"
#include "TestUtils.h"

using namespace std::chrono_literals;

namespace
{
// Generous upper bound for anything to arrive; on localhost it takes
// milliseconds, the margin only matters on overloaded CI machines.
constexpr auto kTimeout = 10s;

// How long to wait before concluding that something was NOT delivered.
constexpr auto kQuietPeriod = 300ms;

const char* const kLocalhost = "127.0.0.1";

ServerConfig TestConfig()
{
  ServerConfig config;
  config.rooms.push_back({.name = "Lobby", .maxUsers = 10, .password = "lobby-pw"});
  config.rooms.push_back({.name = "Game Room", .maxUsers = 2, .password = ""});
  return config;
}

// The server side of EinklangServerApp (see server.cpp), on a free port.
class TestServer
{
public:
  explicit TestServer(const ServerConfig& config = TestConfig(), const std::string& dbPath = {})
  {
    mRooms.InitializeFromConfig(config);
    if (!dbPath.empty())
      mRooms.OpenDatabase(dbPath);

    mServer.PacketHandler().SetLimits(config.maxMessageLength, config.maxUserNameLength);
    LoginConnector::Connect(mServer.PacketHandler(), mRooms);
    ChatConnector::Connect(mServer.PacketHandler(), mRooms);
    VoiceConnector::Connect(mServer.PacketHandler(), mRooms);
    mServer.SetStatusProvider([this]() { return ServerPacketFactory::BuildServerStatus(mRooms.Rooms()); });

    REQUIRE(mServer.Start(0, 64));
    REQUIRE(mServer.Port() != 0);

    mThread = std::thread([this]()
    {
      while (mRunning)
        mServer.ProcessEvents();
    });
  }

  ~TestServer()
  {
    mRunning = false;
    mThread.join();
    mServer.Stop();
  }

  TestServer(const TestServer&) = delete;
  TestServer& operator=(const TestServer&) = delete;

  std::uint16_t Port() const { return mServer.Port(); }

private:
  ChatRoomManager   mRooms;
  EnetServer        mServer;
  std::atomic<bool> mRunning{true};
  std::thread       mThread;
};

struct RelayedVoice
{
  std::uint32_t senderId{};
  std::string   sender;
  std::uint32_t sequence{};
  std::vector<std::uint8_t> opus;
};

// Decodes a relayed voice payload (see VoicePacket.h).
RelayedVoice ParseRelay(const std::vector<std::uint8_t>& payload)
{
  REQUIRE(payload.size() >= Voice::kRelayMinPayloadSize);
  RelayedVoice v;
  std::memcpy(&v.senderId, payload.data(), sizeof(v.senderId));
  const std::size_t nameLen = payload[Voice::kRelaySenderPeerIdSize];
  const std::size_t nameAt = Voice::kRelayPrefixSize;
  REQUIRE(payload.size() >= nameAt + nameLen + Voice::kHeaderSize + 1);
  v.sender.assign(payload.begin() + nameAt, payload.begin() + nameAt + nameLen);
  std::memcpy(&v.sequence, payload.data() + nameAt + nameLen, sizeof(v.sequence));
  v.opus.assign(payload.begin() + nameAt + nameLen + Voice::kHeaderSize, payload.end());
  return v;
}

// A client connection plus a thread-safe record of everything it received.
class TestClient
{
public:
  using Users = std::vector<std::string>;

  struct Received
  {
    std::vector<std::pair<bool, std::string>> entryResponses;
    std::vector<std::pair<std::string, std::string>> chat;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> images;
    std::vector<Users> userUpdates;
    std::vector<std::vector<ChatMessage>> histories;
    std::vector<std::vector<std::uint8_t>> voice;
    bool connectionLost{false};
  };

  TestClient()
  {
    auto& handler = mClient.PacketHandler();
    handler.onRoomEntryResponse.subscribe([this](bool ok, std::string error)
    {
      Record([&](Received& r) { r.entryResponses.emplace_back(ok, std::move(error)); });
    });
    handler.onNewChatMessage.subscribe([this](std::string user, std::string text)
    {
      Record([&](Received& r) { r.chat.emplace_back(std::move(user), std::move(text)); });
    });
    handler.onNewChatImage.subscribe([this](std::string user, std::vector<std::uint8_t> data)
    {
      Record([&](Received& r) { r.images.emplace_back(std::move(user), std::move(data)); });
    });
    handler.onRoomUsersUpdate.subscribe([this](Users users)
    {
      Record([&](Received& r) { r.userUpdates.push_back(std::move(users)); });
    });
    handler.onRoomChatHistory.subscribe([this](std::vector<ChatMessage> history)
    {
      Record([&](Received& r) { r.histories.push_back(std::move(history)); });
    });
    handler.onVoiceFrameReceived.subscribe([this](const std::uint8_t* data, std::size_t size)
    {
      Record([&](Received& r) { r.voice.emplace_back(data, data + size); });
    });
    mClient.onConnectionLost.subscribe([this]()
    {
      Record([&](Received& r) { r.connectionLost = true; });
    });
  }

  ~TestClient() { mClient.Disconnect(); }

  TestClient(const TestClient&) = delete;
  TestClient& operator=(const TestClient&) = delete;

  EnetClient& Client() { return mClient; }

  bool Connect(std::uint16_t port) { return mClient.Connect(kLocalhost, port); }

  void Disconnect() { mClient.Disconnect(); }

  // Waits until pred(Received) holds; false on timeout.
  template <typename Pred>
  bool WaitFor(Pred pred, std::chrono::milliseconds timeout = kTimeout)
  {
    std::unique_lock lock(mMutex);
    return mChanged.wait_for(lock, timeout, [&]() { return pred(mReceived); });
  }

  // Copy of everything received so far.
  Received Snapshot()
  {
    std::lock_guard lock(mMutex);
    return mReceived;
  }

  void Send(const std::vector<std::uint8_t>& packet)
  {
    REQUIRE(mClient.SendPacket(packet));
  }

  void Say(const std::string& text) { Send(ClientPacketFactory::PublishChatMessage(text)); }

  // Sends a room entry request and returns the server's response.
  std::pair<bool, std::string> Join(const std::string& room, const std::string& password,
                                    const std::string& user)
  {
    const auto before = Snapshot().entryResponses.size();
    Send(ClientPacketFactory::RoomEntryRequest(room, password, user));
    REQUIRE(WaitFor([&](const Received& r) { return r.entryResponses.size() > before; }));
    return Snapshot().entryResponses[before];
  }

  // Joins and waits until the user list including `user` has arrived (the
  // history is sent before it, so it is there too).
  void JoinOk(const std::string& room, const std::string& password, const std::string& user)
  {
    const auto [ok, error] = Join(room, password, user);
    REQUIRE_MESSAGE(ok, error);
    REQUIRE(WaitForUsersContaining(user));
  }

  bool WaitForUsers(const Users& users)
  {
    return WaitFor([&](const Received& r) { return !r.userUpdates.empty() && r.userUpdates.back() == users; });
  }

  bool WaitForUsersContaining(const std::string& user)
  {
    return WaitFor([&](const Received& r)
    {
      return !r.userUpdates.empty() && std::ranges::find(r.userUpdates.back(), user) != r.userUpdates.back().end();
    });
  }

  bool WaitForChat(const std::string& user, const std::string& text)
  {
    return WaitFor([&](const Received& r)
    {
      return std::ranges::find(r.chat, std::pair{user, text}) != r.chat.end();
    });
  }

private:
  template <typename F>
  void Record(F f)
  {
    {
      std::lock_guard lock(mMutex);
      f(mReceived);
    }
    mChanged.notify_all();
  }

  std::mutex              mMutex;
  std::condition_variable mChanged;
  Received                mReceived;
  EnetClient              mClient;
};

// Sends voice frames until `receiver` got one (voice is unreliable, so a
// single datagram could in theory be lost even on localhost).
bool SendVoiceUntilReceived(TestClient& sender, TestClient& receiver, std::uint32_t sequence,
                            const std::vector<std::uint8_t>& opus)
{
  const auto before = receiver.Snapshot().voice.size();
  for (int attempt = 0; attempt < 50; ++attempt)
  {
    sender.Client().SendPacket(ClientPacketFactory::VoiceFrame(sequence, opus), Voice::kVoiceChannel, 0);
    if (receiver.WaitFor([&](const TestClient::Received& r) { return r.voice.size() > before; }, 200ms))
      return true;
  }
  return false;
}
}

TEST_SUITE("Integration")
{
TEST_CASE("a client connects and joins a room")
{
  TestServer server;
  TestClient alice;
  REQUIRE(alice.Connect(server.Port()));
  CHECK(alice.Client().IsConnected());
  CHECK(alice.Client().IsConnectedTo(kLocalhost, server.Port()));
  CHECK_FALSE(alice.Client().IsConnectedTo(kLocalhost, server.Port() + 1));

  const auto [ok, error] = alice.Join("Lobby", "lobby-pw", "alice");
  CHECK(ok);
  CHECK(error.empty());

  REQUIRE(alice.WaitForUsers({"alice"}));
  const auto received = alice.Snapshot();
  REQUIRE(received.histories.size() == 1);
  CHECK(received.histories[0].empty());

  alice.Disconnect();
  CHECK_FALSE(alice.Client().IsConnected());
  CHECK_FALSE(alice.Client().IsConnectedTo(kLocalhost, server.Port()));
}

TEST_CASE("room entry is denied with a reason")
{
  auto config = TestConfig();
  config.rooms.push_back({.name = "Tiny", .maxUsers = 1, .password = ""});
  TestServer server(config);

  TestClient alice;
  REQUIRE(alice.Connect(server.Port()));
  alice.JoinOk("Tiny", "", "alice");

  TestClient bob;
  REQUIRE(bob.Connect(server.Port()));

  CHECK(bob.Join("Nowhere", "", "bob") == std::pair{false, std::string("Room does not exist.")});
  CHECK(bob.Join("Lobby", "wrong", "bob") == std::pair{false, std::string("Room password is incorrect.")});
  CHECK(bob.Join("Lobby", "", "bob") == std::pair{false, std::string("Room password is incorrect.")});
  CHECK(bob.Join("Tiny", "", "bob") == std::pair{false, std::string("Room is full.")});

  TestClient alice2;
  REQUIRE(alice2.Connect(server.Port()));
  CHECK(alice2.Join("Lobby", "lobby-pw", "alice") == std::pair{true, std::string()});
  CHECK(bob.Join("Lobby", "lobby-pw", "alice") ==
        std::pair{false, std::string("User name is already in use in this room.")});

  // The same connection can still join after being denied
  CHECK(bob.Join("Lobby", "lobby-pw", "bob") == std::pair{true, std::string()});
}

TEST_CASE("the same user name may be used in different rooms")
{
  TestServer server;
  TestClient a, b;
  REQUIRE(a.Connect(server.Port()));
  REQUIRE(b.Connect(server.Port()));
  a.JoinOk("Lobby", "lobby-pw", "sam");
  b.JoinOk("Game Room", "", "sam");
}

TEST_CASE("chat messages reach everyone in the room and nobody outside it")
{
  TestServer server;
  TestClient alice, bob, carol;
  REQUIRE(alice.Connect(server.Port()));
  REQUIRE(bob.Connect(server.Port()));
  REQUIRE(carol.Connect(server.Port()));
  alice.JoinOk("Lobby", "lobby-pw", "alice");
  bob.JoinOk("Lobby", "lobby-pw", "bob");
  carol.JoinOk("Game Room", "", "carol");

  alice.Say("Hello Lobby! Grüße 👋");
  CHECK(bob.WaitForChat("alice", "Hello Lobby! Grüße 👋"));
  CHECK(alice.WaitForChat("alice", "Hello Lobby! Grüße 👋")); // echoed to the sender

  // Carol's channel is ordered: had the server sent her alice's message, it
  // would arrive before her own.
  carol.Say("marker");
  REQUIRE(carol.WaitForChat("carol", "marker"));
  CHECK(carol.Snapshot().chat == std::vector<std::pair<std::string, std::string>>{{"carol", "marker"}});

  // ...and the marker stays in the Game Room
  bob.Say("done");
  REQUIRE(bob.WaitForChat("bob", "done"));
  for (const auto& [user, text] : bob.Snapshot().chat)
    CHECK(user != "carol");
}

TEST_CASE("messages from one sender arrive in order for everyone")
{
  TestServer server;
  constexpr int kClients = 4;
  constexpr int kMessages = 25;

  std::vector<std::unique_ptr<TestClient>> clients;
  for (int i = 0; i < kClients; ++i)
  {
    clients.push_back(std::make_unique<TestClient>());
    REQUIRE(clients.back()->Connect(server.Port()));
    clients.back()->JoinOk("Lobby", "lobby-pw", "user" + std::to_string(i));
  }

  for (int m = 0; m < kMessages; ++m)
    for (int i = 0; i < kClients; ++i)
      clients[i]->Say(std::to_string(m));

  for (auto& client : clients)
  {
    REQUIRE(client->WaitFor([&](const TestClient::Received& r)
    {
      return r.chat.size() == static_cast<std::size_t>(kClients * kMessages);
    }));

    std::map<std::string, int> next;
    for (const auto& [user, text] : client->Snapshot().chat)
    {
      CHECK(text == std::to_string(next[user]));
      ++next[user];
    }
    CHECK(next.size() == static_cast<std::size_t>(kClients));
  }
}

TEST_CASE("unauthenticated clients cannot chat")
{
  TestServer server;
  TestClient alice, eve;
  REQUIRE(alice.Connect(server.Port()));
  REQUIRE(eve.Connect(server.Port()));
  alice.JoinOk("Lobby", "lobby-pw", "alice");

  // eve's packets are processed in order: the spam comes before her join
  eve.Say("spam before joining");
  eve.Send(ClientPacketFactory::PublishChatImage(std::vector<std::uint8_t>{1, 2, 3}));
  eve.JoinOk("Lobby", "lobby-pw", "eve");
  eve.Say("legit");

  REQUIRE(alice.WaitForChat("eve", "legit"));
  const auto received = alice.Snapshot();
  CHECK(received.chat == std::vector<std::pair<std::string, std::string>>{{"eve", "legit"}});
  CHECK(received.images.empty());
}

TEST_CASE("oversized messages and user names are dropped")
{
  auto config = TestConfig();
  config.maxMessageLength = 16;
  config.maxUserNameLength = 8;
  TestServer server(config);

  TestClient alice;
  REQUIRE(alice.Connect(server.Port()));

  // No answer at all to the oversized name; the next request is answered.
  alice.Send(ClientPacketFactory::RoomEntryRequest("Lobby", "lobby-pw", "a-very-long-name"));
  alice.JoinOk("Lobby", "lobby-pw", "alice");
  CHECK(alice.Snapshot().entryResponses.size() == 1);

  alice.Say(std::string(17, 'x'));
  alice.Say(std::string(16, 'y'));
  REQUIRE(alice.WaitForChat("alice", std::string(16, 'y')));
  CHECK(alice.Snapshot().chat.size() == 1);
}

TEST_CASE("new members receive the recent chat history")
{
  TestServer server;
  TestClient alice;
  REQUIRE(alice.Connect(server.Port()));
  alice.JoinOk("Lobby", "lobby-pw", "alice");

  for (int i = 0; i < 25; ++i)
    alice.Say("message " + std::to_string(i));
  REQUIRE(alice.WaitForChat("alice", "message 24"));

  TestClient bob;
  REQUIRE(bob.Connect(server.Port()));
  bob.JoinOk("Lobby", "lobby-pw", "bob");

  const auto received = bob.Snapshot();
  REQUIRE(received.histories.size() == 1);
  const auto& history = received.histories[0];
  REQUIRE(history.size() == 20); // the server sends the last 20 messages
  CHECK(history.front().text == "message 5");
  CHECK(history.back().text == "message 24");
  for (const auto& msg : history)
  {
    CHECK(msg.userName == "alice");
    CHECK(msg.kind == ChatMessage::Kind::Text);
    CHECK(msg.timestampMs > 0);
  }
  CHECK(received.chat.empty()); // history is not replayed as new messages

  // Other rooms' histories are separate
  TestClient carol;
  REQUIRE(carol.Connect(server.Port()));
  carol.JoinOk("Game Room", "", "carol");
  REQUIRE(carol.Snapshot().histories.size() == 1);
  CHECK(carol.Snapshot().histories[0].empty());
}

TEST_CASE("images are broadcast and kept in the history")
{
  TestServer server;
  TestClient alice, bob;
  REQUIRE(alice.Connect(server.Port()));
  REQUIRE(bob.Connect(server.Port()));
  alice.JoinOk("Lobby", "lobby-pw", "alice");
  bob.JoinOk("Lobby", "lobby-pw", "bob");

  // Larger than one datagram, so ENet has to fragment and reassemble it
  std::vector<std::uint8_t> image(200 * 1024);
  for (std::size_t i = 0; i < image.size(); ++i)
    image[i] = static_cast<std::uint8_t>((i * 7) ^ (i >> 8));
  alice.Send(ClientPacketFactory::PublishChatImage(image));

  REQUIRE(bob.WaitFor([](const TestClient::Received& r) { return !r.images.empty(); }));
  CHECK(bob.Snapshot().images[0].first == "alice");
  CHECK(bob.Snapshot().images[0].second == image);

  TestClient carol;
  REQUIRE(carol.Connect(server.Port()));
  carol.JoinOk("Lobby", "lobby-pw", "carol");
  const auto history = carol.Snapshot().histories.at(0);
  REQUIRE(history.size() == 1);
  CHECK(history[0].kind == ChatMessage::Kind::Image);
  CHECK(history[0].userName == "alice");
  CHECK(history[0].imageData == image);
}

TEST_CASE("the user list follows joins, room switches and disconnects")
{
  TestServer server;
  TestClient alice, bob;
  REQUIRE(alice.Connect(server.Port()));
  REQUIRE(bob.Connect(server.Port()));

  alice.JoinOk("Lobby", "lobby-pw", "alice");
  CHECK(alice.WaitForUsers({"alice"}));

  bob.JoinOk("Lobby", "lobby-pw", "bob");
  CHECK(alice.WaitForUsers({"alice", "bob"}));
  CHECK(bob.WaitForUsers({"alice", "bob"}));

  SUBCASE("switching rooms leaves the old room")
  {
    bob.JoinOk("Game Room", "", "bob");
    CHECK(alice.WaitForUsers({"alice"}));
    CHECK(bob.WaitForUsers({"bob"}));

    // bob only gets Game Room traffic now
    alice.Say("lobby only");
    bob.Say("game only");
    REQUIRE(bob.WaitForChat("bob", "game only"));
    REQUIRE(alice.WaitForChat("alice", "lobby only"));
    CHECK(bob.Snapshot().chat == std::vector<std::pair<std::string, std::string>>{{"bob", "game only"}});
  }
  SUBCASE("a failed switch still leaves the old room")
  {
    CHECK_FALSE(bob.Join("Nowhere", "", "bob").first);
    CHECK(alice.WaitForUsers({"alice"}));
  }
  SUBCASE("disconnecting leaves the room and frees the name")
  {
    bob.Disconnect();
    CHECK(alice.WaitForUsers({"alice"}));

    TestClient bob2;
    REQUIRE(bob2.Connect(server.Port()));
    bob2.JoinOk("Lobby", "lobby-pw", "bob");
    CHECK(alice.WaitForUsers({"alice", "bob"}));
  }
}

TEST_CASE("a room accepts exactly maxUsers members")
{
  TestServer server;
  TestClient a, b, c;
  REQUIRE(a.Connect(server.Port()));
  REQUIRE(b.Connect(server.Port()));
  REQUIRE(c.Connect(server.Port()));
  a.JoinOk("Game Room", "", "a");
  b.JoinOk("Game Room", "", "b");
  CHECK(c.Join("Game Room", "", "c") == std::pair{false, std::string("Room is full.")});

  b.Disconnect();
  REQUIRE(a.WaitForUsers({"a"}));
  CHECK(c.Join("Game Room", "", "c").first);
}

TEST_CASE("voice frames are relayed to the other members of the room")
{
  TestServer server;
  TestClient alice, bob, carol, dave;
  REQUIRE(alice.Connect(server.Port()));
  REQUIRE(bob.Connect(server.Port()));
  REQUIRE(carol.Connect(server.Port()));
  REQUIRE(dave.Connect(server.Port()));
  alice.JoinOk("Lobby", "lobby-pw", "alice");
  bob.JoinOk("Lobby", "lobby-pw", "bob");
  carol.JoinOk("Game Room", "", "carol");
  // dave stays outside every room

  const std::vector<std::uint8_t> opus{0xF8, 0x01, 0x02, 0x03, 0x04, 0x05};
  REQUIRE(SendVoiceUntilReceived(alice, bob, 1234, opus));

  const auto relayed = ParseRelay(bob.Snapshot().voice.front());
  CHECK(relayed.sender == "alice");
  CHECK(relayed.sequence == 1234);
  CHECK(relayed.opus == opus);

  // The sender id is stable per sender and differs between senders
  REQUIRE(SendVoiceUntilReceived(alice, bob, 1235, opus));
  REQUIRE(SendVoiceUntilReceived(bob, alice, 1, opus));
  const auto again = ParseRelay(bob.Snapshot().voice.back());
  const auto fromBob = ParseRelay(alice.Snapshot().voice.back());
  CHECK(again.senderId == relayed.senderId);
  CHECK(fromBob.sender == "bob");
  CHECK(fromBob.senderId != relayed.senderId);

  // Unauthenticated peers' voice goes nowhere
  for (int i = 0; i < 5; ++i)
    dave.Client().SendPacket(ClientPacketFactory::VoiceFrame(i, opus), Voice::kVoiceChannel, 0);

  std::this_thread::sleep_for(kQuietPeriod);
  CHECK(carol.Snapshot().voice.empty()); // other room
  CHECK(dave.Snapshot().voice.empty());  // not in a room
  for (const auto& payload : alice.Snapshot().voice)
    CHECK(ParseRelay(payload).sender == "bob"); // neither her own voice nor dave's
  for (const auto& payload : bob.Snapshot().voice)
    CHECK(ParseRelay(payload).sender == "alice");
}

TEST_CASE("status queries report the rooms and their occupancy")
{
  TestServer server;
  CHECK(StatusProbe::ProbeLocalServer(server.Port(), 5000));

  ServerStatusPoller poller;
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<ServerStatus> updates;
  poller.onServerStatusChanged.subscribe([&](std::string address, std::uint16_t port, ServerStatus status)
  {
    CHECK(address == kLocalhost);
    CHECK(port == server.Port());
    {
      std::lock_guard lock(mutex);
      updates.push_back(std::move(status));
    }
    changed.notify_all();
  });

  auto waitForUpdate = [&](auto pred)
  {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, kTimeout, [&]() { return !updates.empty() && pred(updates.back()); });
  };

  poller.Start();
  poller.SetServers({{kLocalhost, server.Port()}});

  REQUIRE(waitForUpdate([](const ServerStatus& s) { return s.online; }));
  {
    std::lock_guard lock(mutex);
    const auto& status = updates.back();
    CHECK(status.serverVersion == EINKLANG_VERSION_STRING);
    REQUIRE(status.rooms.size() == 2);
    CHECK(status.rooms[0] == RoomStatus{"Lobby", 0, 10, true});
    CHECK(status.rooms[1] == RoomStatus{"Game Room", 0, 2, false});
  }

  TestClient alice;
  REQUIRE(alice.Connect(server.Port()));
  alice.JoinOk("Game Room", "", "alice");
  poller.RequestRefresh();
  CHECK(waitForUpdate([](const ServerStatus& s)
  {
    return s.online && s.rooms.size() == 2 && s.rooms[1].userCount == 1 && s.rooms[0].userCount == 0;
  }));

  poller.Stop();
}

TEST_CASE("servers that do not answer are reported offline")
{
  std::uint16_t port = 0;
  {
    TestServer stopped;
    port = stopped.Port();
  }

  CHECK_FALSE(StatusProbe::ProbeLocalServer(port, 300));

  ServerStatusPoller poller;
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<ServerStatus> updates;
  poller.onServerStatusChanged.subscribe([&](std::string, std::uint16_t, ServerStatus status)
  {
    {
      std::lock_guard lock(mutex);
      updates.push_back(std::move(status));
    }
    changed.notify_all();
  });
  poller.Start();
  poller.SetServers({{kLocalhost, port}});

  std::unique_lock lock(mutex);
  REQUIRE(changed.wait_for(lock, kTimeout, [&]() { return !updates.empty(); }));
  for (const auto& status : updates)
  {
    CHECK_FALSE(status.online);
    CHECK(status.rooms.empty());
  }
  lock.unlock();
  poller.Stop();
}

TEST_CASE("connecting to a port without a server fails cleanly")
{
  std::uint16_t port = 0;
  {
    TestServer stopped;
    port = stopped.Port();
  }

  TestClient alice;
  CHECK_FALSE(alice.Connect(port));
  CHECK_FALSE(alice.Client().IsConnected());
  CHECK_FALSE(alice.Client().SendPacket(ClientPacketFactory::PublishChatMessage("nobody listens")));
}

TEST_CASE("clients notice when the server shuts down and can reconnect")
{
  auto server = std::make_unique<TestServer>();
  TestClient alice;
  REQUIRE(alice.Connect(server->Port()));
  alice.JoinOk("Lobby", "lobby-pw", "alice");

  server.reset();
  REQUIRE(alice.WaitFor([](const TestClient::Received& r) { return r.connectionLost; }));
  CHECK_FALSE(alice.Client().IsConnected());

  server = std::make_unique<TestServer>();
  REQUIRE(alice.Connect(server->Port()));
  alice.JoinOk("Lobby", "lobby-pw", "alice");
}

TEST_CASE("chat history survives a server restart")
{
  TestUtils::TempDir dir;
  const auto dbPath = dir.File("einklang.db");

  {
    TestServer server(TestConfig(), dbPath);
    TestClient alice;
    REQUIRE(alice.Connect(server.Port()));
    alice.JoinOk("Lobby", "lobby-pw", "alice");
    alice.Say("remember me");
    alice.Send(ClientPacketFactory::PublishChatImage(std::vector<std::uint8_t>{4, 5, 6}));
    REQUIRE(alice.WaitFor([](const TestClient::Received& r) { return !r.images.empty(); }));
  }

  TestServer server(TestConfig(), dbPath);
  TestClient bob;
  REQUIRE(bob.Connect(server.Port()));
  bob.JoinOk("Lobby", "lobby-pw", "bob");

  const auto history = bob.Snapshot().histories.at(0);
  REQUIRE(history.size() == 2);
  CHECK(history[0].userName == "alice");
  CHECK(history[0].text == "remember me");
  CHECK(history[1].kind == ChatMessage::Kind::Image);
  CHECK(history[1].imageData == std::vector<std::uint8_t>{4, 5, 6});
}
}
