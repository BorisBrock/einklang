// Simulation tests for the voice jitter buffer.
//
// A real Opus encoder produces 20 ms frames which travel through a simulated
// network (delay, jitter, loss, reordering) and are played out by a simulated
// audio device that requests 10 ms of audio per callback, exactly like the
// miniaudio callback in VoicePlayer.

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <numbers>
#include <print>
#include <random>
#include <vector>

#include "../src/client/Voice/JitterBuffer.h"
#include "../src/client/Voice/VoiceEncoder.h"

namespace
{
int gFailures = 0;

void Check(bool condition, const char* what)
{
  if (!condition)
  {
    std::println("    FAIL: {}", what);
    ++gFailures;
  }
}

struct Packet
{
  std::uint32_t seq;
  std::int64_t  sendUs;
  std::int64_t  arrivalUs;
  std::vector<std::uint8_t> opus;
};

struct NetworkModel
{
  double baseDelayMs{30.0};
  double jitterMs{0.0};      // mean of the exponential extra delay
  double lossRate{0.0};
  double spikeRate{0.0};     // probability of a large delay spike
  double spikeMs{0.0};
};

// Encode `frames` frames of a speech-like test signal starting at `seq0`,
// sent every 20 ms starting at `startMs`.
std::vector<Packet> Transmit(VoiceEncoder& enc, int frames, std::uint32_t seq0,
                             double startMs, const NetworkModel& net, std::mt19937& rng)
{
  std::uniform_real_distribution<double> uni(0.0, 1.0);
  std::exponential_distribution<double> expo(net.jitterMs > 0 ? 1.0 / net.jitterMs : 1.0);

  std::vector<Packet> out;
  std::vector<float> pcm(Voice::kFrameSize);
  std::vector<std::uint8_t> encoded;
  for (int f = 0; f < frames; ++f)
  {
    for (int i = 0; i < Voice::kFrameSize; ++i)
    {
      const double t = (f * Voice::kFrameSize + i) / static_cast<double>(Voice::kSampleRate);
      const double env = 0.5 + 0.5 * std::sin(2 * std::numbers::pi * 3.0 * t);
      pcm[i] = static_cast<float>(0.3 * env * (std::sin(2 * std::numbers::pi * 220.0 * t) +
                                               0.5 * std::sin(2 * std::numbers::pi * 660.0 * t)));
    }
    enc.Encode(pcm, encoded);

    if (uni(rng) < net.lossRate)
      continue;

    double delay = net.baseDelayMs;
    if (net.jitterMs > 0)
      delay += expo(rng);
    if (uni(rng) < net.spikeRate)
      delay += net.spikeMs;

    const double sendMs = startMs + f * Voice::kFrameMs;
    out.push_back({seq0 + static_cast<std::uint32_t>(f),
                   static_cast<std::int64_t>(sendMs * 1000.0),
                   static_cast<std::int64_t>((sendMs + delay) * 1000.0), encoded});
  }
  std::ranges::sort(out, {}, &Packet::arrivalUs);
  return out;
}

struct PlayoutResult
{
  int framesPlayed{0};       // 20 ms frames delivered by the jitter buffer
  int glitches{0};           // silent gaps inside a talk spurt
  double maxLatencyMs{0.0};  // send → playout of the newest frame
  JitterBuffer::Stats stats;
};

// Drive the jitter buffer with a 10 ms device callback until `endMs`.
PlayoutResult Playout(JitterBuffer& jb, const std::vector<Packet>& packets, double endMs)
{
  PlayoutResult r;
  constexpr int kCallbackMs = 10;
  std::vector<float> frame(Voice::kFrameSize);
  std::size_t frameRemaining = 0;
  std::size_t next = 0;
  bool wasPlaying = false;

  for (double nowMs = 0; nowMs < endMs; nowMs += kCallbackMs)
  {
    const auto nowUs = static_cast<std::int64_t>(nowMs * 1000.0);
    while (next < packets.size() && packets[next].arrivalUs <= nowUs)
    {
      jb.Insert(packets[next].seq, packets[next].opus, packets[next].arrivalUs);
      ++next;
    }

    std::size_t need = Voice::kSampleRate * kCallbackMs / 1000;
    bool silent = false;
    while (need > 0)
    {
      if (frameRemaining == 0)
      {
        if (!jb.PullFrame(frame, nowUs))
        {
          silent = true;
          break;
        }
        frameRemaining = Voice::kFrameSize;
        ++r.framesPlayed;
      }
      const std::size_t n = std::min(need, frameRemaining);
      frameRemaining -= n;
      need -= n;
    }

    // A gap is a glitch if the sender was still talking at this point, i.e.
    // a packet that has not arrived yet was already sent.
    if (silent && wasPlaying)
    {
      const bool senderActive = std::ranges::any_of(
          packets.begin() + static_cast<std::ptrdiff_t>(next), packets.end(),
          [nowUs](const Packet& p) { return p.sendUs < nowUs; });
      if (senderActive)
        ++r.glitches;
    }
    wasPlaying = !silent;
  }
  r.stats = jb.GetStats();
  return r;
}

void PrintResult(const PlayoutResult& r)
{
  std::println("    played {}, glitches {}, FEC {}, PLC {}, late {}, dropped {}",
               r.framesPlayed, r.glitches, r.stats.recovered, r.stats.concealed,
               r.stats.late, r.stats.dropped);
}
} // namespace

int main()
{
  VoiceEncoder enc;
  if (!enc.Init())
    return EXIT_FAILURE;
  std::mt19937 rng(1234);

  constexpr int kFrames = 500; // 10 s

  {
    std::println("[1] Ideal network");
    JitterBuffer jb;
    jb.Init();
    const auto pkts = Transmit(enc, kFrames, 0, 0, {}, rng);
    const auto r = Playout(jb, pkts, kFrames * 20.0 + 500);
    PrintResult(r);
    Check(r.glitches == 0, "no glitches");
    Check(r.stats.played == kFrames, "every frame played");
    Check(r.stats.recovered == 0, "no FEC needed");
    Check(r.stats.concealed <= 5, "PLC only for the fade-out tail");
    Check(jb.TargetFrames() == JitterBuffer::kMinTargetFrames, "minimum latency");
  }

  {
    std::println("[2] Jittery network (mean 15 ms jitter, 2% spikes of 80 ms)");
    JitterBuffer jb;
    jb.Init();
    const auto pkts = Transmit(enc, kFrames, 0, 0,
                               {.jitterMs = 15, .spikeRate = 0.02, .spikeMs = 80}, rng);
    const auto r = Playout(jb, pkts, kFrames * 20.0 + 1000);
    PrintResult(r);
    Check(r.glitches == 0, "no audible gaps despite jitter");
    Check(r.stats.late <= kFrames / 50, "only extreme spikes arrive too late");
    Check(jb.TargetFrames() > JitterBuffer::kMinTargetFrames, "target delay adapted upwards");
  }

  {
    std::println("[3] Lossy network (5% loss, 10 ms jitter)");
    JitterBuffer jb;
    jb.Init();
    const auto pkts = Transmit(enc, kFrames, 0, 0, {.jitterMs = 10, .lossRate = 0.05}, rng);
    const auto r = Playout(jb, pkts, kFrames * 20.0 + 1000);
    PrintResult(r);
    Check(r.glitches == 0, "losses are concealed, not silent");
    Check(r.stats.recovered > 0, "FEC used to recover losses");
    Check(r.framesPlayed >= kFrames - 2, "playout timeline is continuous");
  }

  {
    std::println("[4] Two talk spurts separated by silence, sequence continues");
    JitterBuffer jb;
    jb.Init();
    auto pkts = Transmit(enc, 100, 0, 0, {.jitterMs = 5}, rng);
    const auto second = Transmit(enc, 100, 100, 5000, {.jitterMs = 5}, rng);
    pkts.insert(pkts.end(), second.begin(), second.end());
    const auto r = Playout(jb, pkts, 8000);
    PrintResult(r);
    Check(r.glitches == 0, "silence between spurts is not a glitch");
    Check(r.stats.played >= 198, "both spurts played");
    Check(r.stats.late == 0, "second spurt not treated as late");
  }

  {
    std::println("[5] Sender restarts with sequence number 0");
    JitterBuffer jb;
    jb.Init();
    auto pkts = Transmit(enc, 100, 100000, 0, {}, rng);
    const auto second = Transmit(enc, 100, 0, 2100, {}, rng);
    pkts.insert(pkts.end(), second.begin(), second.end());
    const auto r = Playout(jb, pkts, 5000);
    PrintResult(r);
    Check(r.stats.played == 200, "playback resumes after restart");
    Check(r.glitches <= 1, "at most a short re-buffer at the restart");
  }

  {
    std::println("[6] Network stall then burst (latency must recover)");
    JitterBuffer jb;
    jb.Init();
    auto pkts = Transmit(enc, kFrames, 0, 0, {}, rng);
    // Packets sent between 3.0 s and 3.5 s are held back until 3.5 s.
    for (auto& p : pkts)
      if (p.arrivalUs > 3'000'000 && p.arrivalUs < 3'500'000)
        p.arrivalUs = 3'500'000;
    std::ranges::sort(pkts, {}, &Packet::arrivalUs);
    const auto r = Playout(jb, pkts, kFrames * 20.0 + 1000);
    PrintResult(r);
    Check(r.stats.dropped > 0, "excess latency was dropped");
    Check(r.stats.dropped < 30, "only the excess was dropped");
    Check(r.glitches <= 1, "a single gap for the stall");
  }

  if (gFailures == 0)
    std::println("All tests passed.");
  else
    std::println("{} check(s) failed.", gFailures);
  return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
