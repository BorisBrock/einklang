#pragma once

// Internal helpers shared by VoiceCapture and VoicePlayer.  Includes the
// miniaudio declarations, so only include this from .cpp files.

#include <atomic>
#include <cstdint>
#include <string>

#include <miniaudio.h>

#include "AudioDevices.h"

namespace AudioDevices
{
  // Looks up the device of the given type whose name matches `name` and
  // writes its id to `outId`.  Returns false if no such device exists.
  bool FindDeviceId(ma_device_type type, const std::string& name, ma_device_id& outId);

  // Liveness tracking for an open device.  The miniaudio data and
  // notification callbacks only touch the atomics; Poll() runs on a control
  // thread, never on the audio thread, since a device must not be restarted
  // from its own callbacks.
  //
  // Notifications alone are not enough: e.g. on PulseAudio a stream pinned to
  // a removed device simply dies without a "stopped" notification, so a
  // stalled data callback counts as lost as well.
  struct DeviceHealth
  {
    std::atomic<bool>          stopRequested{false};
    std::atomic<bool>          stoppedUnexpectedly{false};
    std::atomic<bool>          rerouted{false};
    std::atomic<std::uint64_t> callbacks{0};

    // Control thread only.
    std::uint64_t lastCallbacks{0};
    int           silentPolls{0};

    // Called before ma_device_start() / ma_device_stop().
    void OnStarting()
    {
      stopRequested.store(false);
      stoppedUnexpectedly.store(false);
      rerouted.store(false);
      callbacks.store(0);
      lastCallbacks = 0;
      silentPolls   = 0;
    }
    void OnStopping() { stopRequested.store(true); }

    // Audio / backend threads.
    void OnDataCallback() { callbacks.fetch_add(1, std::memory_order_relaxed); }
    void OnNotification(ma_device_notification_type type)
    {
      switch (type)
      {
      case ma_device_notification_type_stopped:
        if (!stopRequested.load())
          stoppedUnexpectedly.store(true);
        break;
      case ma_device_notification_type_started:
        // e.g. PulseAudio resuming a suspended stream: it works again.
        stoppedUnexpectedly.store(false);
        break;
      case ma_device_notification_type_rerouted:
        rerouted.store(true);
        break;
      default:
        break;
      }
    }

    // Control thread, called periodically while the device runs.  A device
    // counts as lost after an unexpected stop or after two consecutive polls
    // without a single data callback (allows for slow-starting devices).
    DeviceEvent Poll()
    {
      const std::uint64_t now = callbacks.load(std::memory_order_relaxed);
      silentPolls   = now == lastCallbacks ? silentPolls + 1 : 0;
      lastCallbacks = now;

      if (stoppedUnexpectedly.exchange(false) || silentPolls >= 2)
      {
        silentPolls = 0;
        return DeviceEvent::Lost;
      }
      if (rerouted.exchange(false))
        return DeviceEvent::Rerouted;
      return DeviceEvent::None;
    }
  };
} // namespace AudioDevices
