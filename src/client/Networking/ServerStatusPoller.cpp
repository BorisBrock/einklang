#include "ServerStatusPoller.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <print>
#include <random>
#include <span>

#include <enet.h>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include "../../common/ServerQuery.h"
#include "protocol_generated.h"

namespace
{
// Time between two poll rounds.
constexpr auto kPollInterval = std::chrono::seconds(10);

// A server that has not replied within this time is considered offline.
constexpr enet_uint32 kReplyTimeoutMs = 1500;

// Requests are resent at this interval to servers that have not replied yet,
// so a single lost datagram does not flag a server as offline.
constexpr enet_uint32 kResendIntervalMs = 500;
}

ServerStatusPoller::~ServerStatusPoller()
{
  Stop();
}

void ServerStatusPoller::Start()
{
  if (mThread.joinable())
    return;

  if (enet_initialize() != 0)
  {
    std::println("[Status] Failed to initialize ENet; server status is unavailable.");
    return;
  }

  {
    std::lock_guard lock(mMutex);
    mStopRequested    = false;
    mRefreshRequested = true;
  }
  mThread = std::thread([this]() { Run(); });
}

void ServerStatusPoller::Stop()
{
  if (!mThread.joinable())
    return;

  {
    std::lock_guard lock(mMutex);
    mStopRequested = true;
  }
  mWakeUp.notify_all();
  mThread.join();
  enet_deinitialize();
}

void ServerStatusPoller::SetServers(std::vector<ServerKey> servers)
{
  std::ranges::sort(servers);
  const auto [first, last] = std::ranges::unique(servers);
  servers.erase(first, last);

  {
    std::lock_guard lock(mMutex);
    mServers          = std::move(servers);
    mRefreshRequested = true;
  }
  mWakeUp.notify_all();
}

void ServerStatusPoller::RequestRefresh()
{
  {
    std::lock_guard lock(mMutex);
    mRefreshRequested = true;
  }
  mWakeUp.notify_all();
}

void ServerStatusPoller::Run()
{
  while (true)
  {
    std::vector<ServerKey> servers;
    {
      std::unique_lock lock(mMutex);
      mWakeUp.wait_for(lock, kPollInterval, [this]() { return mStopRequested || mRefreshRequested; });
      if (mStopRequested)
        return;
      mRefreshRequested = false;
      servers = mServers;
    }

    auto results = PollOnce(servers);

    // Forget servers that are no longer polled so they are reported afresh if re-added.
    std::erase_if(mLastStatus, [&](const auto& entry) { return !results.contains(entry.first); });

    for (auto& [key, status] : results)
    {
      auto it = mLastStatus.find(key);
      if (it != mLastStatus.end() && it->second == status)
        continue;

      mLastStatus[key] = status;
      onServerStatusChanged.emit(key.first, key.second, std::move(status));
    }
  }
}

std::map<ServerStatusPoller::ServerKey, ServerStatus>
ServerStatusPoller::PollOnce(const std::vector<ServerKey>& servers)
{
  std::map<ServerKey, ServerStatus> results;
  for (const auto& key : servers)
    results[key] = ServerStatus{}; // offline unless a reply arrives

  if (servers.empty())
    return results;

  const ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (socket == ENET_SOCKET_NULL)
  {
    std::println("[Status] Failed to create status query socket.");
    return results;
  }
  enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
  enet_socket_set_option(socket, ENET_SOCKOPT_IPV6_V6ONLY, 0); // allow IPv4-mapped addresses

  struct PendingQuery
  {
    ServerKey     key;
    ENetAddress   address{};
    std::uint32_t token{0};
    bool          answered{false};
  };

  static thread_local std::mt19937 rng{std::random_device{}()};

  std::vector<PendingQuery> pending;
  pending.reserve(servers.size());
  for (const auto& key : servers)
  {
    PendingQuery query{.key = key};
    if (enet_address_set_host(&query.address, key.first.c_str()) != 0)
      continue; // unresolvable → stays offline
    query.address.port = key.second;
    query.token        = rng();
    pending.push_back(query);
  }

  auto sendUnanswered = [&]()
  {
    for (const auto& query : pending)
    {
      if (query.answered)
        continue;
      auto request = ServerQuery::BuildRequest(query.token);
      ENetBuffer buffer;
      buffer.data       = request.data();
      buffer.dataLength = request.size();
      enet_socket_send(socket, &query.address, &buffer, 1);
    }
  };

  auto handleReply = [&](std::span<const std::uint8_t> data)
  {
    std::uint32_t token = 0;
    std::span<const std::uint8_t> payload;
    if (!ServerQuery::ParseReply(data, token, payload))
      return;

    auto it = std::ranges::find_if(pending, [token](const PendingQuery& q)
    {
      return !q.answered && q.token == token;
    });
    if (it == pending.end())
      return;

    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<Einklang::ServerStatus>(nullptr))
      return;

    const auto* reply = flatbuffers::GetRoot<Einklang::ServerStatus>(payload.data());
    ServerStatus status{.online = true};
    if (reply->server_version() != nullptr)
      status.serverVersion = reply->server_version()->str();
    if (reply->rooms() != nullptr)
    {
      for (const auto* room : *reply->rooms())
      {
        if (room == nullptr || room->name() == nullptr)
          continue;
        status.rooms.push_back({room->name()->str(), room->user_count(), room->max_users()});
      }
    }

    it->answered     = true;
    results[it->key] = std::move(status);
  };

  sendUnanswered();

  const enet_uint32 start      = enet_time_get();
  enet_uint32       nextResend = start + kResendIntervalMs;

  while (std::ranges::any_of(pending, [](const PendingQuery& q) { return !q.answered; }))
  {
    const enet_uint32 now     = enet_time_get();
    const enet_uint32 elapsed = ENET_TIME_DIFFERENCE(now, start);
    if (elapsed >= kReplyTimeoutMs)
      break;

    if (ENET_TIME_GREATER_EQUAL(now, nextResend))
    {
      sendUnanswered();
      nextResend += kResendIntervalMs;
    }

    const enet_uint32 untilResend = ENET_TIME_DIFFERENCE(nextResend, now);
    const enet_uint32 waitMs      = std::min(kReplyTimeoutMs - elapsed, untilResend);

    enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
    if (enet_socket_wait(socket, &condition, waitMs) != 0)
      break;
    if ((condition & ENET_SOCKET_WAIT_RECEIVE) == 0)
      continue;

    // Drain every datagram that is ready.
    while (true)
    {
      std::array<std::uint8_t, ServerQuery::kMaxReplySize> data{};
      ENetBuffer buffer;
      buffer.data       = data.data();
      buffer.dataLength = data.size();
      ENetAddress from{};

      const int received = enet_socket_receive(socket, &from, &buffer, 1);
      if (received == -2)
        continue; // truncated / interrupted datagram — skip it
      if (received <= 0)
        break;
      handleReply({data.data(), static_cast<std::size_t>(received)});
    }
  }

  enet_socket_destroy(socket);
  return results;
}
