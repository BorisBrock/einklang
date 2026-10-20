# Client Configuration

[← Back to the README](../README.md)

The client stores its settings in `client-config.yml` next to the executable (the AppImage uses `~/.config/einklang/client-config.yml`; `EINKLANG_CLIENT_CONFIG` overrides the path). The client creates the file on first start and shows a welcome screen to pick your name and microphone. Most of these settings can be changed in the client's Settings dialog.

## Keys

| Key | Description |
|---|---|
| `user-name` | Your name in all chat rooms (defaults to the OS login name; editable under Settings → Profile) |
| `voice-enabled` | Enable/disable voice chat on the client |
| `input-gain` | Microphone gain multiplier |
| `output-volume` | Speaker volume multiplier |
| `vad-min-rms-floor` | Minimum energy before the voice-activity gate can open (default `0.005`) |
| `vad-snr-factor` | Gate threshold relative to the estimated noise floor (default `4.0`) |
| `vad-attack-frames` / `vad-release-frames` | Frames needed to open / held open after speech stops |
| `vad-zcr-min` / `vad-zcr-max` | Speech-like zero-crossing-rate band (crossings/s) |
| `noise-filter` | High-pass IIR filter to remove low-frequency noise (fan hum, etc.) |
| `known-rooms` | Saved server connections (address, port, room, password) |
