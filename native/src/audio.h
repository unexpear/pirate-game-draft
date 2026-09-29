// Sea Trial — thin miniaudio wrapper. Public domain (Unlicense).
//
// Placeholder sounds are synthesized in memory (no asset files): a short cannon
// crack, and a seamless looping wind/ocean bed. init() tries a real playback
// device, then miniaudio's no-device engine. Playback calls are no-ops when
// the engine is down, and silence when there is no device or Mute is on.
#pragma once

namespace audio {

enum class OneShot { Cannon };

struct Status {
    bool ready = false;   // engine is up (hardware device or no-device fallback)
    bool device = false;  // a real playback device was opened
    bool muted = false;
    bool bed = false;     // wind/ocean loop is running
};

struct InitOptions {
    // Skip the hardware attempt and mix only through ma_engine_read_pcm_frames.
    // The headless test uses this so it can read the mix with or without a card.
    bool forceNullDevice = false;
};

struct Clip {
    const float* samples = nullptr; // interleaved float32
    int frames = 0;
    int channels = 0;
    int sampleRate = 0;
};

// False only when even the no-device engine cannot start. A missing sound
// card still returns true, with status().device == false.
bool init(InitOptions options = {});
void shutdown();

// Smooth the bed toward `bedGain` (0..1, before Mute) and, on the no-device
// engine, pump that many seconds of the graph.
void update(float dt, float bedGain);

void setMuted(bool muted);
void setBedEnabled(bool enabled);
void playOneShot(OneShot shot);

Status status();

// Absolute peak of the most recent no-device pump. Always 0 when a hardware
// device is consuming the graph (there is no side buffer to inspect).
float lastNullPeak();

// Procedural clips. Valid for the rest of the process; no device required.
Clip cannonClip();
Clip bedClip();

}
