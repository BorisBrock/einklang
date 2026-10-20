#include <print>
#include <vector>

#include "../../common/ChatMessage.h"
#include "../GUI/WebViewModel.h"
#include "../Networking/ClientPacketFactory.h"
#include "../Networking/EnetClient.h"
#include "ChatConnector.h"

void ChatConnector::Connect(WebViewModel& viewModel, EnetClient& enetClient)
{
  // JS → Server: forward outgoing chat messages
  viewModel.onChatMessageSubmitted.subscribe([&](std::string msg)
  {
    std::println("Chat message submitted: {}", msg);

    const auto packet = ClientPacketFactory::PublishChatMessage(msg);
    if (!enetClient.SendPacket(packet))
    {
      std::println("Failed to send chat message packet.");
    }
  });

  // JS → Server: forward outgoing chat images
  viewModel.onChatImageSubmitted.subscribe([&](std::vector<std::uint8_t> imageData)
  {
    std::println("Chat image submitted: {} bytes.", imageData.size());

    const auto packet = ClientPacketFactory::PublishChatImage(imageData);
    if (!enetClient.SendPacket(packet))
    {
      std::println("Failed to send chat image packet.");
    }
  });

  // Server → GUI: forward incoming chat messages
  enetClient.PacketHandler().onNewChatMessage.subscribe([&viewModel](std::string userName, std::string text)
  {
    viewModel.NotifyNewChatMessage(userName, text);
  });

  // Server → GUI: forward incoming chat images
  enetClient.PacketHandler().onNewChatImage.subscribe([&viewModel](std::string userName,
                                                                    std::vector<std::uint8_t> imageData)
  {
    viewModel.NotifyNewChatImage(userName, std::move(imageData));
  });

  // Server → GUI: forward incoming user-list updates
  enetClient.PacketHandler().onRoomUsersUpdate.subscribe([&viewModel](std::vector<std::string> users)
  {
    viewModel.NotifyRoomUsersUpdate(std::move(users));
  });

  // Server → GUI: forward the room's chat history on entry
  enetClient.PacketHandler().onRoomChatHistory.subscribe([&viewModel](std::vector<ChatMessage> entries)
  {
    viewModel.NotifyRoomChatHistory(std::move(entries));
  });
}