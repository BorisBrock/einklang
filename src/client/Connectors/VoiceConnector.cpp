#include "VoiceConnector.h"

#include <print>

#include "../GUI/WebViewModel.h"
#include "../Networking/EnetClient.h"
#include "../Voice/VoicePipeline.h"

void VoiceConnector::Connect(WebViewModel& viewModel, EnetClient& enetClient,
                              VoicePipeline& voicePipeline, bool voiceEnabled)
{
  // Provide the view model with a way to poll the microphone input RMS level
  // for the live level-meter in the settings dialog.
  viewModel.SetInputRmsGetter([&voicePipeline]() { return voicePipeline.GetInputRms(); });

  if (!voiceEnabled)
  {
    std::println("[Voice] Voice chat disabled by client configuration.");
    return;
  }

  // Start voice when the user successfully enters a room
  viewModel.onRoomEntryResult.subscribe(
    [&voicePipeline, &viewModel](bool success, const std::string& /*errorMessage*/)
    {
      if (success)
      {
        // Tell the pipeline who the local user is so it can emit onSpeakerActivity
        // for locally-captured frames (VAD indicator for the speaking client itself).
        voicePipeline.SetLocalUserName(viewModel.GetLocalUserName());

        if (!voicePipeline.IsRunning())
        {
          if (!voicePipeline.Start())
            std::println("[Voice] Warning: failed to start voice pipeline for room.");
        }
      }
    });

  // Stop voice when the active room is left (e.g. room removal).
  // Immediately restart capture so the settings dialog level-meter preview
  // continues to work even when not connected to a room.
  viewModel.onActiveRoomLeft.subscribe(
    [&voicePipeline]()
    {
      voicePipeline.Stop();
      voicePipeline.Start(); // restart capture-only for settings preview
    });

  // Mute/unmute microphone from the GUI
  viewModel.onVoiceMuteToggled.subscribe(
    [&voicePipeline](bool muted)
    {
      voicePipeline.SetMuted(muted);
      std::println("[Voice] Microphone {}.", muted ? "muted" : "unmuted");
    });

  // Apply voice settings changed from the settings dialog
  viewModel.onVoiceSettingsChanged.subscribe(
    [&voicePipeline](float vadThreshold, float inputGain)
    {
      voicePipeline.SetVadThreshold(vadThreshold);
      voicePipeline.SetInputGain(inputGain);
      std::println("[Voice] Settings updated \u2014 VAD threshold: {:.4f}, input gain: {:.2f}.",
                   vadThreshold, inputGain);
    });

  // Mark users as speaking in the GUI when remote voice frames decode.
  voicePipeline.onSpeakerActivity.subscribe(
    [&viewModel](const std::string& userName)
    {
      viewModel.NotifyUserSpeaking(userName, true);
    });

  // Forward incoming voice frames from the network to the pipeline
  enetClient.PacketHandler().onVoiceFrameReceived.subscribe(
    [&voicePipeline](const std::uint8_t* data, std::size_t size)
    {
      voicePipeline.HandleIncomingVoiceFrame(data, size);
    });

  // Start capture once the WebView2 message loop is running so that
  // VoicePlayer::Start() → ma_device_init() (WASAPI) is always called
  // from inside the COM STA context established by saucer/WebView2.
  // Calling it before webUI.Run() would initialise COM as MTA on the main
  // thread, silently preventing WebView2 from opening the window on Windows.
  viewModel.onReady.subscribe(
    [&voicePipeline]()
    {
      if (!voicePipeline.IsRunning())
      {
        if (!voicePipeline.Start())
          std::println("[Voice] Warning: failed to start initial voice capture.");
      }
    });
}
