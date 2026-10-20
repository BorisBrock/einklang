# Voice Pipeline

[← Back to the README](../README.md)

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
