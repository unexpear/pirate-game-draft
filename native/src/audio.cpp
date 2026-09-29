// Sea Trial — thin miniaudio wrapper. Public domain (Unlicense).
#include "audio.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace audio {
namespace {

constexpr int kRate = 48000;
constexpr int kVoices = 6;
constexpr float kPi = 3.14159265f;
constexpr double kPiD = 3.141592653589793;
constexpr ma_uint32 kSoundFlags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;

struct Rng {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return float((s >> 8) & 0xFFFFFFu) / float(0x1000000) * 2.0f - 1.0f;
    }
};

struct Clips {
    bool built = false;
    std::vector<float> cannon; // mono
    std::vector<float> bed;    // stereo interleaved
    int cannonFrames = 0;
    int bedFrames = 0;
};

struct State {
    bool ready = false;
    bool device = false;
    bool engineUp = false;
    bool muted = false;
    bool bedOn = false;
    float bedGain = 0.5f;
    float nullPeak = 0.0f;
    ma_engine engine{};
    ma_audio_buffer_ref cannonRef[kVoices]{};
    ma_sound cannon[kVoices]{};
    bool cannonRefOk[kVoices]{};
    bool cannonOk[kVoices]{};
    int nextVoice = 0;
    ma_audio_buffer_ref bedRef{};
    ma_sound bed{};
    bool bedRefOk = false;
    bool bedOk = false;
    std::vector<float> nullMix;
};

Clips gClips;
State g;

float onePoleA(float cutoffHz) {
    return std::exp(-2.0f * kPi * cutoffHz / float(kRate));
}

void normalize(std::vector<float>& samples, float targetPeak) {
    float peak = 0.0f;
    for (float v : samples) peak = std::max(peak, std::fabs(v));
    if (peak < 1.0e-8f) return;
    const float gain = targetPeak / peak;
    for (float& v : samples) v *= gain;
}

void loopCrossfade(std::vector<float>& interleaved, int channels, int frames, int fade) {
    for (int i = 0; i < fade; ++i) {
        const float w = float(i) / float(fade); // 0 at the join, ~1 at the end of the window
        for (int c = 0; c < channels; ++c) {
            const float head = interleaved[i * channels + c];
            const float tail = interleaved[(frames + i) * channels + c];
            interleaved[i * channels + c] = tail * (1.0f - w) + head * w;
        }
    }
    interleaved.resize(size_t(frames * channels));
}

void buildClips() {
    if (gClips.built) return;

    // Cannon: a noise crack plus a falling low thump. ~280 ms, mono.
    const int cannonN = int(0.28f * kRate);
    gClips.cannon.resize(size_t(cannonN));
    Rng crack{0xC4A001u};
    float phase = 0.0f;
    float prevN = 0.0f;
    for (int i = 0; i < cannonN; ++i) {
        const float t = float(i) / float(kRate);
        const float body = std::exp(-t * 12.0f);
        const float snap = std::exp(-t * 70.0f);
        const float tick = std::exp(-t * 140.0f);
        const float freq = 60.0f + 110.0f * std::exp(-t * 16.0f);
        phase += freq / float(kRate);
        const float n = crack.next();
        const float hp = n - prevN;
        prevN = n;
        const float thump = std::sin(phase * 2.0f * kPi) * body;
        const float click = std::sin(t * 1850.0f * 2.0f * kPi) * tick;
        gClips.cannon[size_t(i)] = thump * 0.85f + hp * snap * 0.7f + click * 0.18f;
    }
    const int fadeIn = int(0.0015f * kRate);
    const int fadeOut = int(0.018f * kRate);
    for (int i = 0; i < fadeIn; ++i) gClips.cannon[size_t(i)] *= float(i) / float(fadeIn);
    for (int i = 0; i < fadeOut; ++i) gClips.cannon[size_t(cannonN - 1 - i)] *= float(i) / float(fadeOut);
    normalize(gClips.cannon, 0.90f);
    gClips.cannonFrames = cannonN;

    // Wind/ocean bed: 4 s stereo, one exact swell cycle so the loop LFO matches.
    // Extra tail is overlap-added onto the head after filtering, so the join
    // is the natural continuation of the last sample.
    const int bedN = 4 * kRate;
    const int fade = int(0.080f * kRate);
    const int total = bedN + fade;
    std::vector<float> raw(size_t(total * 2));
    Rng wind{0x051E0Cu};
    float brownL = 0.0f, brownR = 0.0f;
    float airL = 0.0f, airR = 0.0f;
    const float aBrown = onePoleA(90.0f);
    const float aAir = onePoleA(650.0f);
    for (int i = 0; i < total; ++i) {
        const double t = double(i) / double(kRate);
        const float swell = 0.78f + 0.22f * float(std::sin(2.0 * kPiD * 0.25 * t));
        const float chop = 0.88f + 0.12f * float(std::sin(2.0 * kPiD * 0.50 * t + 0.7));
        const float nL = wind.next();
        const float nR = wind.next();
        brownL = brownL * aBrown + nL * (1.0f - aBrown);
        brownR = brownR * aBrown + nR * (1.0f - aBrown);
        airL = airL * aAir + nL * (1.0f - aAir);
        airR = airR * aAir + nR * (1.0f - aAir);
        raw[size_t(i * 2)]     = (brownL * 1.15f + airL * 0.35f) * swell;
        raw[size_t(i * 2 + 1)] = (brownR * 1.15f + airR * 0.35f) * swell * chop;
    }
    // High-pass the rumble so a DC wander doesn't thump at the loop point.
    const float aHp = onePoleA(35.0f);
    float yL = 0.0f, yR = 0.0f, xL = 0.0f, xR = 0.0f;
    for (int i = 0; i < total; ++i) {
        const float inL = raw[size_t(i * 2)];
        const float inR = raw[size_t(i * 2 + 1)];
        yL = aHp * (yL + inL - xL);
        yR = aHp * (yR + inR - xR);
        xL = inL;
        xR = inR;
        raw[size_t(i * 2)] = yL;
        raw[size_t(i * 2 + 1)] = yR;
    }
    loopCrossfade(raw, 2, bedN, fade);
    normalize(raw, 0.40f);
    gClips.bed = std::move(raw);
    gClips.bedFrames = bedN;
    gClips.built = true;
}

// miniaudio's default backend list ends at the null backend, which "opens"
// successfully and discards samples. That is silence, not a playback device.
bool audibleDevice(ma_engine* engine) {
    ma_device* device = ma_engine_get_device(engine);
    if (device == nullptr) return false;
    ma_context* ctx = ma_device_get_context(device);
    if (ctx == nullptr) return false;
    return ctx->backend != ma_backend_null;
}

const char* backendName(ma_engine* engine) {
    ma_device* device = ma_engine_get_device(engine);
    ma_context* ctx = device ? ma_device_get_context(device) : nullptr;
    if (ctx == nullptr) return "unknown";
    const char* name = ma_get_backend_name(ctx->backend);
    return name != nullptr ? name : "unknown";
}

bool startEngine(bool forceNull) {
    ma_engine_config cfg = ma_engine_config_init();
    cfg.channels = 2;
    cfg.sampleRate = kRate;

    if (!forceNull) {
        ma_result result = ma_engine_init(&cfg, &g.engine);
        if (result == MA_SUCCESS && !audibleDevice(&g.engine)) {
            std::fprintf(stderr,
                         "audio: no audible playback device (backend %s); no-device fallback (silent)\n",
                         backendName(&g.engine));
            std::fflush(stderr);
            ma_engine_uninit(&g.engine);
            result = MA_NO_DEVICE;
        }
        if (result == MA_SUCCESS) {
            g.device = true;
            g.engineUp = true;
            const ma_uint32 rate = ma_engine_get_sample_rate(&g.engine);
            const ma_uint32 ch = ma_engine_get_channels(&g.engine);
            std::printf("audio: playback device open via %s (%u Hz, %u ch)\n",
                        backendName(&g.engine), rate, ch);
            std::fflush(stdout);
            return true;
        }
        if (result != MA_NO_DEVICE) {
            std::fprintf(stderr, "audio: playback device unavailable (%s); no-device fallback (silent)\n",
                         ma_result_description(result));
            std::fflush(stderr);
        }
    }

    cfg = ma_engine_config_init();
    cfg.noDevice = MA_TRUE;
    cfg.channels = 2;
    cfg.sampleRate = kRate;
    const ma_result result = ma_engine_init(&cfg, &g.engine);
    if (result != MA_SUCCESS) {
        std::fprintf(stderr, "audio: no-device engine failed (%s)\n", ma_result_description(result));
        std::fflush(stderr);
        return false;
    }
    g.device = false;
    g.engineUp = true;
    return true;
}

bool startSounds() {
    buildClips();
    for (int i = 0; i < kVoices; ++i) {
        ma_result result = ma_audio_buffer_ref_init(
            ma_format_f32, 1, gClips.cannon.data(), ma_uint64(gClips.cannonFrames), &g.cannonRef[i]);
        if (result != MA_SUCCESS) return false;
        g.cannonRef[i].sampleRate = kRate;
        g.cannonRefOk[i] = true;
        result = ma_sound_init_from_data_source(&g.engine, &g.cannonRef[i], kSoundFlags, nullptr, &g.cannon[i]);
        if (result != MA_SUCCESS) return false;
        g.cannonOk[i] = true;
        ma_sound_set_volume(&g.cannon[i], 0.95f);
    }

    ma_result result = ma_audio_buffer_ref_init(
        ma_format_f32, 2, gClips.bed.data(), ma_uint64(gClips.bedFrames), &g.bedRef);
    if (result != MA_SUCCESS) return false;
    g.bedRef.sampleRate = kRate;
    g.bedRefOk = true;
    result = ma_sound_init_from_data_source(&g.engine, &g.bedRef, kSoundFlags | MA_SOUND_FLAG_LOOPING, nullptr, &g.bed);
    if (result != MA_SUCCESS) return false;
    g.bedOk = true;
    ma_sound_set_looping(&g.bed, MA_TRUE);
    ma_sound_set_volume(&g.bed, g.bedGain * 0.55f);
    if (ma_sound_start(&g.bed) != MA_SUCCESS) return false;
    g.bedOn = true;
    return true;
}

} // namespace

bool init(InitOptions options) {
    if (g.ready) return true;
    buildClips();
    if (!startEngine(options.forceNullDevice)) return false;
    if (!startSounds()) {
        shutdown();
        return false;
    }
    g.ready = true;
    g.muted = false;
    ma_engine_set_volume(&g.engine, 1.0f);
    return true;
}

void shutdown() {
    for (int i = 0; i < kVoices; ++i) {
        if (g.cannonOk[i]) {
            ma_sound_stop(&g.cannon[i]);
            ma_sound_uninit(&g.cannon[i]);
        }
        if (g.cannonRefOk[i]) ma_audio_buffer_ref_uninit(&g.cannonRef[i]);
        g.cannonOk[i] = false;
        g.cannonRefOk[i] = false;
    }
    if (g.bedOk) {
        ma_sound_stop(&g.bed);
        ma_sound_uninit(&g.bed);
    }
    if (g.bedRefOk) ma_audio_buffer_ref_uninit(&g.bedRef);
    g.bedOk = false;
    g.bedRefOk = false;
    g.bedOn = false;
    if (g.engineUp) ma_engine_uninit(&g.engine);
    g.engineUp = false;
    g.ready = false;
    g.device = false;
    g.muted = false;
    g.nullPeak = 0.0f;
    g.nullMix.clear();
    g.nextVoice = 0;
}

void update(float dt, float bedGain) {
    if (!g.ready) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    const float target = std::clamp(bedGain, 0.0f, 1.0f);
    const float k = 1.0f - std::exp(-dt * 3.0f);
    g.bedGain += (target - g.bedGain) * k;
    if (g.bedOk) ma_sound_set_volume(&g.bed, g.bedGain * 0.55f);

    if (g.device || dt <= 0.0f) {
        if (g.device) g.nullPeak = 0.0f;
        return;
    }
    const ma_uint32 rate = ma_engine_get_sample_rate(&g.engine);
    const ma_uint32 ch = ma_engine_get_channels(&g.engine);
    if (rate == 0 || ch == 0) return;
    const ma_uint32 frames = ma_uint32(std::max(1, int(dt * float(rate))));
    g.nullMix.resize(size_t(frames) * size_t(ch));
    ma_engine_read_pcm_frames(&g.engine, g.nullMix.data(), frames, nullptr);
    float peak = 0.0f;
    for (float s : g.nullMix) peak = std::max(peak, std::fabs(s));
    g.nullPeak = peak;
}

void setMuted(bool muted) {
    g.muted = muted;
    if (g.ready) ma_engine_set_volume(&g.engine, muted ? 0.0f : 1.0f);
}

void setBedEnabled(bool enabled) {
    if (!g.ready || !g.bedOk) {
        g.bedOn = enabled;
        return;
    }
    if (enabled && !g.bedOn) ma_sound_start(&g.bed);
    else if (!enabled && g.bedOn) ma_sound_stop(&g.bed);
    g.bedOn = enabled;
}

void playOneShot(OneShot shot) {
    if (shot != OneShot::Cannon) return;
    if (!g.ready || g.muted) return;
    const int v = g.nextVoice;
    g.nextVoice = (g.nextVoice + 1) % kVoices;
    if (!g.cannonOk[v]) return;
    ma_sound_stop(&g.cannon[v]);
    ma_sound_seek_to_pcm_frame(&g.cannon[v], 0);
    ma_sound_start(&g.cannon[v]);
}

Status status() {
    Status s;
    s.ready = g.ready;
    s.device = g.device;
    s.muted = g.muted;
    s.bed = g.bedOn;
    return s;
}

float lastNullPeak() { return g.nullPeak; }

Clip cannonClip() {
    buildClips();
    Clip c;
    c.samples = gClips.cannon.data();
    c.frames = gClips.cannonFrames;
    c.channels = 1;
    c.sampleRate = kRate;
    return c;
}

Clip bedClip() {
    buildClips();
    Clip c;
    c.samples = gClips.bed.data();
    c.frames = gClips.bedFrames;
    c.channels = 2;
    c.sampleRate = kRate;
    return c;
}

} // namespace audio
