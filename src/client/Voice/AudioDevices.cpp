// NOTE: Do NOT define MINIAUDIO_IMPLEMENTATION here — it is already defined
// in VoiceCapture.cpp.
#include "AudioDevices.h"
#include "AudioDevicesInternal.h"

#include <print>

namespace
{
// Runs `fn(playbackInfos, playbackCount, captureInfos, captureCount)` on a
// temporary miniaudio context.  Returns false if enumeration failed.
template <typename Fn>
bool WithDevices(Fn&& fn)
{
  ma_context context;
  if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS)
  {
    std::println("[Voice] Failed to initialise audio context for device enumeration.");
    return false;
  }

  ma_device_info* playbackInfos = nullptr;
  ma_uint32       playbackCount = 0;
  ma_device_info* captureInfos  = nullptr;
  ma_uint32       captureCount  = 0;
  const bool ok = ma_context_get_devices(&context, &playbackInfos, &playbackCount,
                                         &captureInfos, &captureCount) == MA_SUCCESS;
  if (ok)
    fn(playbackInfos, playbackCount, captureInfos, captureCount);
  else
    std::println("[Voice] Failed to enumerate audio devices.");

  // The device infos are owned by the context, so they must be consumed above.
  ma_context_uninit(&context);
  return ok;
}
} // namespace

AudioDevices::DeviceList AudioDevices::Enumerate()
{
  DeviceList list;
  WithDevices([&](ma_device_info* playback, ma_uint32 playbackCount,
                  ma_device_info* capture, ma_uint32 captureCount)
  {
    for (ma_uint32 i = 0; i < captureCount; ++i)
      list.inputs.push_back({capture[i].name, capture[i].isDefault != MA_FALSE});
    for (ma_uint32 i = 0; i < playbackCount; ++i)
      list.outputs.push_back({playback[i].name, playback[i].isDefault != MA_FALSE});
  });
  return list;
}

bool AudioDevices::FindDeviceId(ma_device_type type, const std::string& name, ma_device_id& outId)
{
  bool found = false;
  WithDevices([&](ma_device_info* playback, ma_uint32 playbackCount,
                  ma_device_info* capture, ma_uint32 captureCount)
  {
    ma_device_info* infos = type == ma_device_type_capture ? capture : playback;
    const ma_uint32 count = type == ma_device_type_capture ? captureCount : playbackCount;
    for (ma_uint32 i = 0; i < count && !found; ++i)
    {
      if (name == infos[i].name)
      {
        // The id is a plain value, so it stays valid after the context is gone.
        outId = infos[i].id;
        found = true;
      }
    }
  });
  return found;
}
