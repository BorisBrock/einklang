# Einklang

*The next generation voice chat for gamers.*

A real-time voice and text chat application built in C++23, consisting of a server (`EinklangServerApp`) and a client (`EinklangClientApp`).

> Einklang (German: *harmony, unison*) is the successor of the earlier *Reverb* project.

## Key Features

Why choose Einklang? Here is how it stands out from alternatives like Discord, TeamSpeak, and Mumble:

- **100% Free and Open-Source (FOSS):** No upfront costs, no subscriptions, and absolutely no ads.
- **Privacy-First:** No telemetry or hidden data collection. Both the client and server are entirely under your control.
- **Multi-Channel Servers:** Create and manage multiple chat rooms on a single server.
- **Persistent Rich Text Chat:** Message history is securely saved, complete with support for links, images, and more.
- **High-Quality Voice Chat:** Enjoy crystal-clear, ultra-low-latency voice communication.
- **Lightweight and Efficient:** Say goodbye to bloatware. Einklang takes up only a few megabytes of disk space, and both the client and server are highly optimized for minimal CPU and RAM usage.
- **Cross-Platform:** Currently available for Linux and Windows, with macOS support planned.
- **Hassle-Free Setup:** Get started instantly with portable versions—just download and run. Hosting your own server is just as simple, featuring an easy-to-deploy Docker image and a single-port requirement for effortless network configuration.

## Dependencies

All dependencies are fetched automatically via CMake's `FetchContent` and pinned to exact releases — no manual installation required.

| Library | Version | Purpose |
|---|---|---|
| [saucer](https://github.com/saucer/saucer) | 8.0.5 | Cross-platform WebView GUI (client) |
| [ENet](https://github.com/zpl-c/enet) | 2.7.0 | UDP networking |
| [FlatBuffers](https://github.com/google/flatbuffers) | 25.12.19 | Binary protocol serialization |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | 0.9.0 | Configuration file parsing |
| [SQLite](https://www.sqlite.org) | 3.54.1 | Chat history persistence (server) |
| [miniaudio](https://github.com/mackron/miniaudio) | 0.11.25 | Audio I/O (client) |
| [Opus](https://github.com/xiph/opus) | 1.6.1 | Low-latency audio codec (client) |

## Prerequisites

- CMake 3.25+
- A C++23-capable compiler with `<print>` (GCC 14+, Clang 18+, MSVC 19.40+)
- Linux client only: WebKitGTK 6.0, GTK 4 and libadwaita development packages
- GDB (for debugging on Linux)

---

## Building

The build system copies the relevant config files next to each binary automatically after every build:
- `build/<variant>/client-config.yml`
- `build/<variant>/server-config.yml`

### Step 1 — Select a build variant

**In VS Code (recommended):**
1. Open the Command Palette (`Ctrl+Shift+P`)
2. Run **CMake: Select Variant**
3. Choose **Debug** (debug symbols, no optimizations) or **Release** (optimized)

The active variant is also shown in the status bar at the bottom of the window (e.g. `[Release]`). Click it there to switch directly.

**In the terminal:**
No separate step needed — the variant is passed at configure time (see below).

---

### Step 2 — Configure

Only required the first time per variant, or after changes to `CMakeLists.txt`.

**In VS Code:**
1. `Ctrl+Shift+P` → **CMake: Configure**

CMake Tools automatically uses `build/Debug` or `build/Release` based on the active variant.

**In the terminal:**
```bash
# Debug
cmake -B build/Debug -DCMAKE_BUILD_TYPE=Debug

# Release
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release
```

---

### Step 3 — Build

**In VS Code:**
- Press `Ctrl+Shift+B` (runs the default build task), **or**
- `Ctrl+Shift+P` → **CMake: Build**, **or**
- Click the **Build** button (hammer icon) in the status bar

**In the terminal:**
```bash
# Debug
cmake --build build/Debug --parallel

# Release
cmake --build build/Release --parallel
```

---

### Tests

```bash
ctest --test-dir build/Debug --output-on-failure
```

`JitterBufferTest` streams real Opus frames through a simulated network
(jitter, loss, delay spikes, stalls) into the voice jitter buffer and checks
that playback stays gap-free.

---

## Running

Always run the binaries from their build directory so they can find their config files. Start the server before the client.

**In VS Code:**
1. Open the **Run and Debug** panel (`Ctrl+Shift+D`)
2. Select **Debug Server** from the dropdown and press **F5** — this builds and launches the server under GDB
3. Select **Debug Client** from the dropdown and press **F5** — this builds and launches the client under GDB

VS Code supports multiple concurrent debug sessions, so both can run at the same time.

To run **without** the debugger attached, use `Ctrl+F5` instead of `F5`.

**In the terminal:**
```bash
# Debug variant
cd build/Debug
./EinklangServerApp   # start the server first
./EinklangClientApp   # then the client

# Release variant
cd build/Release
./EinklangServerApp
./EinklangClientApp
```

---

## Configuration

### Server — `src/server/server-config.yml`

| Key | Description |
|---|---|
| `port` | UDP port the server listens on (default: `49152`) |
| `maxHistoryMessages` | Number of chat messages retained per room |
| `maxMessageLength` | Maximum length of a single chat message |
| `maxUserNameLength` | Maximum username length |
| `voiceEnabled` | Enable/disable voice chat globally |
| `rooms` | List of rooms, each with `name`, `maxUsers`, `password`, `voiceEnabled` |

### Client — `src/client/client-config.yml`

| Key | Description |
|---|---|
| `voice-enabled` | Enable/disable voice chat on the client |
| `input-gain` | Microphone gain multiplier |
| `output-volume` | Speaker volume multiplier |
| `vad-min-rms-floor` | Minimum energy before the voice-activity gate can open (default `0.005`) |
| `vad-snr-factor` | Gate threshold relative to the estimated noise floor (default `4.0`) |
| `vad-attack-frames` / `vad-release-frames` | Frames needed to open / held open after speech stops |
| `vad-zcr-min` / `vad-zcr-max` | Speech-like zero-crossing-rate band (crossings/s) |
| `noise-filter` | High-pass IIR filter to remove low-frequency noise (fan hum, etc.) |
| `known-rooms` | Saved server connections (address, port, room, username, password) |

---

## Voice Pipeline

```
Mic → HPF → gain → VAD gate → Opus (FEC) → ENet ch. 2 (unreliable) → server relay
    → client network thread → lock-free inbox → audio callback:
      per-sender adaptive jitter buffer → Opus decode / FEC / PLC → mix → speaker
```

- **Decoding is driven by the audio output clock.** The playback callback pulls
  exactly as many 20 ms frames as the device consumes, so network timing never
  leaks into playback timing.
- **Adaptive jitter buffer** (`src/client/Voice/JitterBuffer.*`): packets are
  ordered by sequence number; the playout delay follows the observed jitter
  (40–240 ms) and grows smoothly when packets start arriving later; excess
  latency after a network stall is dropped.
- **Loss handling:** a missing frame is rebuilt from the next packet's in-band
  FEC data, otherwise Opus packet-loss concealment is used.
- **No ENet packet throttling:** ENet's default throttle randomly discards
  unreliable packets whenever round-trip time fluctuates; it is pinned open for
  every peer because voice handles loss itself.
- The audio threads never lock or allocate.

---

## Debugging in VS Code

The workspace ships two launch configurations usable from the **Run and Debug** panel (`Ctrl+Shift+D`):

| Configuration | Binary |
|---|---|
| **Debug Client** | `EinklangClientApp` |
| **Debug Server** | `EinklangServerApp` |

Both configurations:
- Automatically trigger a build before launching
- Set the working directory to the active variant's build folder (so config files are found)
- Attach GDB with pretty-printing and Intel disassembly flavour enabled

**Typical debug workflow:**
1. Set breakpoints by clicking in the gutter next to any source line
2. Select **Debug Server** → press **F5** — the server starts and pauses at any breakpoints
3. Select **Debug Client** → press **F5** — the client starts in a second debug session
4. Use the **Debug toolbar** (or keyboard shortcuts) to step, continue, inspect variables, and view the call stack

> **Tip:** The **Debug Console** (`Ctrl+Shift+Y`) accepts GDB/MI commands directly while a session is paused.
