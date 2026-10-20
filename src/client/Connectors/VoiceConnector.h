#pragma once

class WebViewModel;
class EnetClient;
class VoicePipeline;

// Wires the client voice pipeline to the networking and view model events.
// - Starts the pipeline when the client successfully enters a room.
// - Stops it on disconnect / room leave.
// - Forwards incoming voice frames from the server to the pipeline.
class VoiceConnector
{
public:
  static void Connect(WebViewModel& viewModel, EnetClient& enetClient,
                      VoicePipeline& voicePipeline, bool voiceEnabled);
};
