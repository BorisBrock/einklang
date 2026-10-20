#pragma once

#include <cstdint>

// Sends a status query (see common/ServerQuery.h) to a server on this host
// and waits for the reply. Used by `EinklangServerApp --healthcheck` so a
// container healthcheck verifies the server actually answers on its port.
namespace StatusProbe
{
bool ProbeLocalServer(std::uint16_t port, std::uint32_t timeoutMs);
}
