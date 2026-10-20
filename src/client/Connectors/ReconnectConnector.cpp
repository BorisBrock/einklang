#include "ReconnectConnector.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <print>
#include <string>
#include <thread>

#include "../GUI/WebViewModel.h"
#include "../Networking/EnetClient.h"

namespace
{
// Delay before each reconnect attempt; the last value repeats indefinitely.
constexpr std::array kRetryDelays{
  std::chrono::seconds(1), std::chrono::seconds(2), std::chrono::seconds(4),
  std::chrono::seconds(8), std::chrono::seconds(15)};

struct ReconnectState
{
  // Bumped on every new connection loss so stale retry timers do nothing.
  std::atomic<std::uint64_t> generation{0};
  std::atomic<std::size_t>   attempt{0};
};

void ScheduleAttempt(WebViewModel& viewModel, const std::shared_ptr<ReconnectState>& state)
{
  const auto generation = state->generation.load();
  const auto attempt    = state->attempt.load();
  const auto delay      = kRetryDelays[std::min(attempt, kRetryDelays.size() - 1)];

  std::thread([&viewModel, state, generation, attempt, delay]()
  {
    std::this_thread::sleep_for(delay);
    if (state->generation != generation || !viewModel.IsReconnecting())
      return;

    std::println("[Reconnect] Attempt {} to re-enter the active room...", attempt + 1);
    viewModel.ReconnectToActiveRoom();
  }).detach();
}
}

void ReconnectConnector::Connect(WebViewModel& viewModel, EnetClient& enetClient)
{
  auto state = std::make_shared<ReconnectState>();

  // Fired on the ENet event-loop thread
  enetClient.onConnectionLost.subscribe([&viewModel, state]()
  {
    if (!viewModel.BeginReconnect())
      return;

    std::println("[Reconnect] Connection to the active room lost; reconnecting.");
    ++state->generation;
    state->attempt = 0;
    ScheduleAttempt(viewModel, state);
  });

  viewModel.onReconnectAttemptFailed.subscribe([&viewModel, state](const std::string& errorMessage)
  {
    std::println("[Reconnect] Attempt failed: {}", errorMessage);
    ++state->attempt;
    ScheduleAttempt(viewModel, state);
  });
}
