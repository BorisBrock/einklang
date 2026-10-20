# Building Einklang

[← Back to the README](../README.md)

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

## Building

The build system copies the server config file next to the binary automatically after every build (`build/<variant>/server-config.yml`). The client writes its own `client-config.yml` on first start.

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
ctest --test-dir build/Debug --output-on-failure --parallel 4
```

The C++ tests use [doctest](https://github.com/doctest/doctest), fetched like
the other dependencies. Every executable accepts doctest's options, e.g.
`build/Debug/ServerTests -tc="*history*"` to run matching test cases or
`--success` to list every assertion.

| Test | What it covers |
|---|---|
| `CommonTests` | `src/common`: Base64, events, the status query wire format, the lock-free ring buffer |
| `ServerTests` | `server-config.yml` loading and saving, the SQLite chat database (persistence, history limits, images, schema migration), rooms, decoding of client packets (built with the real client code, plus malformed and random input) and the status payload |
| `ClientTests` | `client-config.yml` loading and saving, encoding of client packets, decoding of server packets |
| `IntegrationTests` | A real server and real clients talking over UDP on localhost: room entry, chat, images, history, user lists, voice relay, status queries, persistence across restarts and reconnecting |
| `JitterBufferTest` | Streams real Opus frames through a simulated network (jitter, loss, delay spikes, stalls) into the voice jitter buffer and checks that playback stays gap-free. Built with the client only. |
| `JoinCodeTest` | Round-trips join codes through `src/client/GUI/Assets/joincode.js` and checks that `tools/join-code-generator.html` carries an identical copy of the codec. Needs Node.js (skipped if `node` isn't found). After changing `joincode.js`, update the copy with `node tests/JoinCodeTest.js --sync`. |

All but `JitterBufferTest` also build with `-DEINKLANG_BUILD_CLIENT=OFF`. The
integration tests bind their servers to ports chosen by the OS, so several
test runs can share a machine.

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

## CI and releases

- **CI** (`.github/workflows/ci.yml`) builds everything and runs the tests on Linux and Windows for every pull request and push to `main`, runs the server/networking tests again under AddressSanitizer and UndefinedBehaviorSanitizer, and checks that the Docker image builds.
- **Release** (`.github/workflows/release.yml`) runs when a version tag is pushed. The tag is the single source of truth for the version — there is no version file to bump:

  ```bash
  git tag v1.0.0 && git push origin v1.0.0
  ```

  It publishes a GitHub release with `Einklang-<version>-x86_64.AppImage` (Linux client), `EinklangClient_Portable_Windows_<version>.zip` and `EinklangServer_Portable_Windows_<version>.zip`, and pushes `borisbrock/einklang:<version>` (plus `<major>.<minor>`, `<major>` and `latest`) to Docker Hub. Tags with a suffix (e.g. `v1.1.0-rc1`) become pre-releases and don't move `latest`. It needs the secrets `DOCKERHUB_USERNAME` and `DOCKERHUB_TOKEN`.

The version is set at configure time: `-DEINKLANG_VERSION=1.2.3` (what the release workflow and the `release-*` scripts pass), otherwise `git describe` (e.g. `1.0.0-3-gabc1234`), or `0.0.0-dev` without git metadata. Docker builds take it as `--build-arg EINKLANG_VERSION=1.2.3`. The client shows it in the window title and the Settings dialog; the server prints it on startup and reports it in status queries.

The app icon comes from `icon.svg` (detailed) and `icon-small.svg` (32 px and below); `packaging/iconrender.cpp` renders them into PNGs and an ICO at build time for the Windows executables and the AppImage, and the client shows `icon.svg` in its empty chat view.

The AppImage keeps its configuration in `~/.config/einklang/client-config.yml` (the bundle itself is read-only); `EINKLANG_CLIENT_CONFIG` overrides the config path for any build. Running `packaging/linux/build-appimage.sh <build-dir> <version>` builds the AppImage locally.
