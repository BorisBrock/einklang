#include "StatusProbe.h"

#include <array>
#include <random>
#include <span>

#include <enet.h>

#include "../../common/ServerQuery.h"

bool StatusProbe::ProbeLocalServer(std::uint16_t port, std::uint32_t timeoutMs)
{
  if (enet_initialize() != 0)
    return false;

  bool answered = false;

  const ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (socket != ENET_SOCKET_NULL)
  {
    enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(socket, ENET_SOCKOPT_IPV6_V6ONLY, 0); // allow IPv4-mapped addresses

    ENetAddress address{};
    enet_address_set_host(&address, "127.0.0.1");
    address.port = port;

    const std::uint32_t token   = std::random_device{}();
    auto                request = ServerQuery::BuildRequest(token);
    ENetBuffer          requestBuffer;
    requestBuffer.data       = request.data();
    requestBuffer.dataLength = request.size();
    enet_socket_send(socket, &address, &requestBuffer, 1);

    const enet_uint32 start = enet_time_get();
    while (!answered)
    {
      const enet_uint32 elapsed = ENET_TIME_DIFFERENCE(enet_time_get(), start);
      if (elapsed >= timeoutMs)
        break;

      enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
      if (enet_socket_wait(socket, &condition, timeoutMs - elapsed) != 0)
        break;
      if ((condition & ENET_SOCKET_WAIT_RECEIVE) == 0)
        continue;

      std::array<std::uint8_t, ServerQuery::kMaxReplySize> data{};
      ENetBuffer buffer;
      buffer.data       = data.data();
      buffer.dataLength = data.size();
      const int received = enet_socket_receive(socket, nullptr, &buffer, 1);
      if (received <= 0)
        continue;

      std::uint32_t replyToken = 0;
      std::span<const std::uint8_t> payload;
      answered = ServerQuery::ParseReply({data.data(), static_cast<std::size_t>(received)}, replyToken, payload)
                 && replyToken == token;
    }

    enet_socket_destroy(socket);
  }

  enet_deinitialize();
  return answered;
}
