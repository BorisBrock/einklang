#include <algorithm>
#include <format>
#include <string>
#include <vector>
#include <saucer/smartview.hpp>
#include <saucer/embedded/all.hpp>
#ifdef _WIN32
#  include <windows.h>
// Provide the win32 stable_natives<window> specialization without including
// webview2.hpp, which would drag in the WebView2 SDK header (not in our path).
// module.hpp (pulled in by smartview.hpp) declares the base template.
namespace saucer { template <> struct stable_natives<window> { HWND hwnd; }; }
#else
#  include <unistd.h>
#endif
#include "JsUtils.h"
#include "WebUI.h"
#include "WebViewModel.h"
#include "../../common/Base64.h"
#include "../../common/ChatMessage.h"
#include "../Configuration/ConfigManager.h"


WebUI::WebUI(WebViewModel& viewModel, ConfigManager& configManager)
  : mViewModel(viewModel), mConfigManager(configManager)
{
}

int WebUI::Run()
{
  auto start = [this](saucer::application *app) -> coco::stray {
    auto window = saucer::window::create(app).value();
    auto webview = saucer::smartview::create({.window = window});

    mExecuteJs = [&webview](const std::string& js) { webview->saucer::webview::execute(js); };

    window->set_title("Einklang");
    {
      auto& cfg = mConfigManager.Get();
      window->set_size({cfg.windowWidth, cfg.windowHeight});
    }
#ifdef _WIN32
    // Apply the embedded .ico resource (ID 1) to the window title bar and
    // taskbar button.  We load both the large (32×32) and small (16×16)
    // variants so every DPI context gets the right size.
    {
      auto *const hwnd = window->native<true>().hwnd;
      auto loadIco = [](int cx, int cy) -> HICON {
        return static_cast<HICON>(
          LoadImage(GetModuleHandle(nullptr), MAKEINTRESOURCE(1),
                    IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
      };
      if (auto* hBig = loadIco(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)))
        SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
      if (auto* hSmall = loadIco(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)))
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
    }
#endif

    // Track the current window size so we can persist it on close
    saucer::size currentSize = window->size();
    window->on<saucer::window::event::resize>([&currentSize](int w, int h) {
      currentSize = {w, h};
    });
    window->on<saucer::window::event::closed>([this, &currentSize]() {
      auto& cfg = mConfigManager.Get();
      cfg.windowWidth  = currentSize.w;
      cfg.windowHeight = currentSize.h;
      mConfigManager.Save();
    });

    webview->embed(saucer::embedded::all());
    webview->serve("/main.html");

    // --- VM → GUI: translate ViewModel events into JS execute() calls ---

    mViewModel.onRoomAdded.subscribe([this](const ClientConfig::KnownRoom& room)
    {
      if (!mExecuteJs) return;
      mExecuteJs(std::format("addKnownRoom(\"{}\", {}, \"{}\", \"{}\")",
                             JsUtils::EscapeString(room.address),
                             room.port,
                             JsUtils::EscapeString(room.roomName),
                             JsUtils::EscapeString(room.userName)));
    });

    mViewModel.onRoomEntryResult.subscribe([this](bool success, const std::string& errorMessage)
    {
      if (!mExecuteJs) return;
      mExecuteJs(std::format("handleRoomEntryResponse({}, \"{}\")",
                             success ? "true" : "false",
                             JsUtils::EscapeString(errorMessage)));
    });

    mViewModel.onActiveRoomChanged.subscribe([this](const std::string& address, uint16_t port,
                                                    const std::string& roomName)
    {
      if (!mExecuteJs) return;
      mExecuteJs(std::format("setActiveRoom(\"{}\", {}, \"{}\")",
                             JsUtils::EscapeString(address),
                             port,
                             JsUtils::EscapeString(roomName)));
    });

    mViewModel.onNewChatMessage.subscribe([this](const std::string& userName, const std::string& text)
    {
      if (!mExecuteJs) return;
      mExecuteJs(std::format("appendChatMessage(\"{}\", \"{}\")",
                             JsUtils::EscapeString(userName),
                             JsUtils::EscapeString(text)));
    });

    mViewModel.onNewChatImage.subscribe([this](const std::string& userName,
                                               const std::vector<std::uint8_t>& imageData)
    {
      if (!mExecuteJs) return;
      const std::string b64 = Base64::Encode(imageData);
      mExecuteJs(std::format("appendChatImage(\"{}\", \"{}\")",
                             JsUtils::EscapeString(userName),
                             b64));
    });

    mViewModel.onRoomChatHistory.subscribe([this](const std::vector<ChatMessage>& entries)
    {
      if (!mExecuteJs) return;
      std::string jsArray = "[";
      for (std::size_t i = 0; i < entries.size(); ++i)
      {
        if (i > 0) jsArray += ",";
        const auto& e = entries[i];
        if (e.kind == ChatMessage::Kind::Image)
        {
          const std::string b64 = Base64::Encode(e.imageData);
          jsArray += std::format(R"({{"userName":"{}","imageData":"{}"}})",
                                 JsUtils::EscapeString(e.userName),
                                 b64);
        }
        else
        {
          jsArray += std::format(R"({{"userName":"{}","text":"{}"}})",
                                 JsUtils::EscapeString(e.userName),
                                 JsUtils::EscapeString(e.text));
        }
      }
      jsArray += "]";
      mExecuteJs("loadChatHistory(" + jsArray + ")");
    });

    mViewModel.onRoomUsersUpdate.subscribe([this](const std::vector<std::string>& users)
    {
      if (!mExecuteJs) return;
      std::string js = "setRoomUsers([";
      for (std::size_t i = 0; i < users.size(); ++i)
      {
        if (i > 0) js += ",";
        js += "\"" + JsUtils::EscapeString(users[i]) + "\"";
      }
      js += "])";
      mExecuteJs(js);
    });

    mViewModel.onUserSpeaking.subscribe([this](const std::string& userName, bool isSpeaking)
    {
      if (!mExecuteJs) return;
      mExecuteJs(std::format("setUserSpeaking(\"{}\", {})",
                             JsUtils::EscapeString(userName),
                             isSpeaking ? "true" : "false"));
    });

    // When the active room is removed, tell the GUI to reset to the idle state.
    mViewModel.onActiveRoomLeft.subscribe([this]()
    {
      if (!mExecuteJs) return;
      mExecuteJs("clearActiveRoom()");
    });

    // --- JS → VM: forward exposed JS functions into the ViewModel ---

    webview->expose("publish_chat_msg", [this](std::string msg) {
      mViewModel.HandleChatMessage(std::move(msg));
    });

    webview->expose("publish_chat_image", [this](std::string b64) {
      auto imageData = Base64::Decode(b64);
      if (!imageData.empty())
        mViewModel.HandleChatImage(std::move(imageData));
    });

    webview->expose("connect_to_server", [this](std::string server, int port, std::string room,
                                                std::string password, std::string username) {
      mViewModel.HandleConnectToServer(std::move(server), static_cast<uint16_t>(port),
                                       std::move(room), std::move(password), std::move(username));
    });

    webview->expose("switch_to_room", [this](std::string address, int port,
                                             std::string roomName, std::string userName) {
      mViewModel.HandleSwitchToRoom(std::move(address), static_cast<uint16_t>(port),
                                    std::move(roomName), std::move(userName));
    });

    webview->expose("get_known_rooms", [this]() -> std::string {
      return mViewModel.GetKnownRoomsJson();
    });

    webview->expose("toggle_voice_mute", [this](bool muted) {
      mViewModel.HandleVoiceMuteToggle(muted);
    });

    webview->expose("get_voice_enabled", [this]() -> bool {
      return mConfigManager.Get().voiceEnabled;
    });

    webview->expose("get_voice_settings", [this]() -> std::string {
      const auto& cfg = mConfigManager.Get();
      return std::format(R"({{"vadThreshold":{},"inputGain":{}}})",
                         cfg.vadMinRmsFloor, cfg.inputGain);
    });

    webview->expose("apply_voice_settings", [this](double vad, double gain) {
      mViewModel.HandleSetVoiceSettings(static_cast<float>(vad),
                                        static_cast<float>(gain));
    });

    webview->expose("save_voice_settings", [this](double vad, double gain) {
      auto& cfg = mConfigManager.Get();
      cfg.vadMinRmsFloor = static_cast<float>(vad);
      cfg.inputGain    = static_cast<float>(gain);
      mConfigManager.Save();
      mViewModel.HandleSetVoiceSettings(static_cast<float>(vad),
                                        static_cast<float>(gain));
    });

    webview->expose("get_input_rms", [this]() -> double {
      return static_cast<double>(mViewModel.GetInputRms());
    });

    webview->expose("remove_room", [this](std::string address, int port, std::string roomName) {
      // Remove from persisted config first
      auto& cfg = mConfigManager.Get();
      std::erase_if(cfg.knownRooms, [&](const ClientConfig::KnownRoom& r)
      {
        return r.address == address &&
               r.port    == static_cast<uint16_t>(port) &&
               r.roomName == roomName;
      });
      mConfigManager.Save();

      // Update the view-model (fires onActiveRoomLeft if this was the active room)
      mViewModel.HandleRemoveRoom(std::move(address), static_cast<uint16_t>(port),
                                  std::move(roomName));
    });

    webview->expose("open_url", [](std::string url) {
      // Only allow http/https — block file://, javascript:, etc.
      const bool isHttp  = url.size() > 7  && url.substr(0, 7)  == "http://";
      const bool isHttps = url.size() > 8  && url.substr(0, 8)  == "https://";
      if (!isHttp && !isHttps) return;
#ifdef _WIN32
      ::ShellExecuteW(nullptr, L"open",
                      std::wstring(url.begin(), url.end()).c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
      if (::fork() == 0) { ::execl("/usr/bin/open", "open", url.c_str(), nullptr); ::_exit(0); }
#else
      if (::fork() == 0) { ::execl("/usr/bin/xdg-open", "xdg-open", url.c_str(), nullptr); ::_exit(0); }
#endif
    });

    webview->expose("remove_server", [this](std::string address, int port) {
      // Remove all rooms for this server from persisted config
      auto& cfg = mConfigManager.Get();
      std::erase_if(cfg.knownRooms, [&](const ClientConfig::KnownRoom& r)
      {
        return r.address == address && r.port == static_cast<uint16_t>(port);
      });
      mConfigManager.Save();

      // Update the view-model (fires onActiveRoomLeft if the active room was on this server)
      mViewModel.HandleRemoveServer(std::move(address), static_cast<uint16_t>(port));
    });

    webview->expose("edit_server", [this](std::string oldAddress, int oldPort,
                                          std::string newAddress, int newPort,
                                          std::string userName) -> std::string {
      if (newAddress.empty() || newPort < 1 || newPort > 65535 || userName.size() > 64)
        return mViewModel.GetKnownRoomsJson();

      // Update persisted config first
      auto& cfg = mConfigManager.Get();
      if (WebViewModel::EditServerRooms(cfg.knownRooms, oldAddress, static_cast<uint16_t>(oldPort),
                                        newAddress, static_cast<uint16_t>(newPort), userName))
        mConfigManager.Save();

      // Update the view-model (fires onActiveRoomLeft if the active room was on this server)
      return mViewModel.HandleEditServer(oldAddress, static_cast<uint16_t>(oldPort),
                                         newAddress, static_cast<uint16_t>(newPort), userName);
    });

    window->show();

    // Signal that the WebView2 STA context is now established so that
    // subscribers (e.g. VoiceConnector) can safely call into WASAPI/COM.
    mViewModel.onReady.emit();

    co_await app->finish();
  };

#ifdef _WIN32
  const auto pid = GetCurrentProcessId();
#else
  const auto pid = ::getpid();
#endif
  // The ID must be a valid GApplication ID (contain at least one dot, each
  // segment starting with a letter) so that saucer passes it to GApplication
  // verbatim on Linux.  Appending the PID makes every process instance unique,
  // preventing GTK's single-instance enforcement from killing the second client.
  const auto appId = "app.einklang.client" + std::to_string(pid);
  return saucer::application::create({.id = appId})->run(start);
}