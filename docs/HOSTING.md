# Hosting a Server

[← Back to the README](../README.md)

There are two ready-made ways to run the server:

- **Docker** (Linux): the image [`borisbrock/einklang`](https://hub.docker.com/r/borisbrock/einklang), see [Docker](#docker) below.
- **Windows**: download `EinklangServer_Portable_Windows_<version>.zip` from the [releases page](https://github.com/BorisBrock/einklang/releases/latest), unzip it and start `EinklangServerApp.exe`.

## Network

The server needs exactly **one UDP port** (the `port` from `server-config.yml`, default `24816`). Chat, images, voice and server-status queries all use it; no TCP ports are involved. Clients only make outgoing connections and need no port forwarding.

To make a home-hosted server reachable from the internet:

1. Forward **UDP 24816** in your router to the machine running the server (for Proxmox: the VM/LXC's own IP, and allow the port in the Proxmox firewall if it is enabled).
2. Use a DynDNS hostname if your public IP changes; clients resolve the address on every connect.
3. On DS-Lite connections (no public IPv4), clients must connect via IPv6 — open the port for the server host in the router's IPv6 firewall instead. The server listens on IPv4 and IPv6.

## Docker

Run the prebuilt image:

```bash
docker run -d --name einklang-server --network host -v /path/to/data:/data --restart unless-stopped borisbrock/einklang:latest
```

Or build it from this repository with Docker Compose:

```bash
docker compose up -d --build
```

`docker-compose.yml` uses host networking, so the server binds its UDP port directly on the host (no Docker NAT for voice traffic, IPv6 works out of the box). Adjust the `/data` volume path to your system; on first start a default `server-config.yml` is placed there. The image's healthcheck (`EinklangServerApp --healthcheck`) sends a real status query to the server, so the container is only reported healthy while it answers.

> **Note:** Traffic is not encrypted. Room passwords, chat and voice travel in plain text, so treat a public server accordingly (or run it behind a VPN such as WireGuard/Tailscale).

## Server configuration — `server-config.yml`

| Key | Description |
|---|---|
| `port` | UDP port the server listens on (default: `24816`) |
| `maxConnections` | Optional cap on simultaneous connections; default is the sum of all rooms' `maxUsers` plus 16 |
| `maxHistoryMessages` | Number of chat messages retained per room |
| `maxMessageLength` | Maximum length of a single chat message |
| `maxUserNameLength` | Maximum username length |
| `voiceEnabled` | Enable/disable voice chat globally |
| `rooms` | List of rooms, each with `name`, `maxUsers`, `password`, `voiceEnabled` |

