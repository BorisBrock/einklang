#pragma once

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../common/Event.h"
#include "../../common/ServerStatus.h"

// Periodically queries every known server with a connectionless status
// request (see common/ServerQuery.h) on a background thread and reports
// whether it is online and how many users are in each of its rooms.
class ServerStatusPoller
{
public:
  using ServerKey = std::pair<std::string, std::uint16_t>; // address, port

  ~ServerStatusPoller();

  void Start();
  void Stop();

  // Replaces the set of servers to poll and triggers an immediate poll round.
  // Safe to call from any thread.
  void SetServers(std::vector<ServerKey> servers);

  // Triggers an immediate poll round. Safe to call from any thread.
  void RequestRefresh();

  // Fired from the polling thread whenever a server's status changes
  // (and once for every newly added server).
  // Args: address, port, status
  Event<std::string, std::uint16_t, ServerStatus> onServerStatusChanged;

private:
  void Run();
  std::map<ServerKey, ServerStatus> PollOnce(const std::vector<ServerKey>& servers);

  std::mutex              mMutex;
  std::condition_variable mWakeUp;
  bool                    mStopRequested{false};
  bool                    mRefreshRequested{false};
  std::vector<ServerKey>  mServers;
  std::thread             mThread;

  // Only accessed from the polling thread.
  std::map<ServerKey, ServerStatus> mLastStatus;
};
