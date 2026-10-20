#pragma once

#include <string>
#include <vector>

// Enumeration of the system's audio devices.
//
// Devices are identified by their display name, which is what the client
// persists in its configuration.  An empty name always means "use the system
// default device", which also follows the OS when the default changes.
namespace AudioDevices
{
  struct DeviceInfo
  {
    std::string name;
    bool        isDefault{false};
  };

  struct DeviceList
  {
    std::vector<DeviceInfo> inputs;
    std::vector<DeviceInfo> outputs;
  };

  // What happened to an open device since it was last polled.
  enum class DeviceEvent
  {
    None,
    Rerouted, // the backend moved the stream to another device; still running
    Lost,     // stopped unexpectedly or stopped delivering audio (unplugged?)
  };

  // Queries the audio backend for all capture and playback devices.
  // Returns empty lists if the backend cannot be initialised.
  DeviceList Enumerate();
} // namespace AudioDevices
