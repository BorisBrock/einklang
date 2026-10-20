#include <print>
#include <string_view>
#include <enet.h>
#include "../common/Version.h"
#include "Networking/EnetClient.h"
#include "GUI/WebUI.h"
#include "GUI/WebViewModel.h"
#include "Configuration/ConfigManager.h"
#include "Connectors/ChatConnector.h"
#include "Connectors/ConnectionConnector.h"
#include "Connectors/RoomEntryConnector.h"
#include "Connectors/VoiceConnector.h"
#include "Voice/VoicePipeline.h"

int main(int argc, char* argv[])
{
    std::println("Einklang Client v{}", EINKLANG_VERSION_STRING);

    // ---------------------------------------------------------------------------
    // Parse optional CLI arguments
    //   -playback <file>  — stream a WAV/audio file as microphone input (loops)
    //   -capture  <file>  — write received audio to a WAV file instead of speakers
    // Both flags are independent and can be combined.
    // ---------------------------------------------------------------------------
    VoicePipelineOptions voiceOpts;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg(argv[i]);
        if ((arg == "-playback" || arg == "--playback") && i + 1 < argc)
        {
            voiceOpts.playbackFile = argv[++i];
            std::println("[CLI] Playback source: {}", voiceOpts.playbackFile);
        }
        else if ((arg == "-capture" || arg == "--capture") && i + 1 < argc)
        {
            voiceOpts.captureFile = argv[++i];
            std::println("[CLI] Capture output : {}", voiceOpts.captureFile);
        }
    }

    // Load configuration
    ConfigManager configManager("client-config.yml");
    if (!configManager.Load())
    {
        std::println("Failed to load config.");
        return EXIT_FAILURE;
    }

    // Instantiate components
    EnetClient enetClient{};
    WebViewModel viewModel{};
    WebUI webUI(viewModel, configManager);
    VoicePipeline voicePipeline(enetClient, std::move(voiceOpts));
    const bool voiceEnabled = configManager.Get().voiceEnabled;

    // Initialise voice pipeline (capture device, encoder, decoder, playback)
    if (voiceEnabled && !voicePipeline.Init())
    {
        std::println("Warning: voice pipeline initialisation failed; voice chat will be unavailable.");
    }

    if (voiceEnabled)
        voicePipeline.ApplyConfig(configManager.Get());

    // Seed room list from config before starting the GUI
    viewModel.SetKnownRooms(configManager.Get().knownRooms);

    // Connect components
    ChatConnector::Connect(viewModel, enetClient);
    ConnectionConnector::Connect(viewModel, enetClient, configManager);
    RoomEntryConnector::Connect(enetClient, viewModel, configManager);
    VoiceConnector::Connect(viewModel, enetClient, voicePipeline, voiceEnabled);

    // Enter GUI main loop
    int result = webUI.Run();

    // Cleanup
    voicePipeline.Stop();
    enetClient.Disconnect();

    return result;
}