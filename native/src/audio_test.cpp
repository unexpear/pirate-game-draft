// Sea Trial — headless audio smoke test. Public domain (Unlicense).
//
// No sound card required. Checks the procedural clips, then inits miniaudio
// (real device if one exists, otherwise the no-device engine) and a forced
// null engine whose mix is read back.
#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

int fail(const std::string& msg) {
    std::fprintf(stderr, "audio_headless FAIL: %s\n", msg.c_str());
    return 1;
}

float rms(const float* s, int n) {
    if (n <= 0) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += double(s[i]) * double(s[i]);
    return float(std::sqrt(acc / double(n)));
}

float peakAbs(const float* s, int n) {
    float p = 0.0f;
    for (int i = 0; i < n; ++i) p = std::max(p, std::fabs(s[i]));
    return p;
}

int checkClips() {
    const audio::Clip cannon = audio::cannonClip();
    const audio::Clip bed = audio::bedClip();
    if (cannon.samples == nullptr || cannon.channels != 1 || cannon.sampleRate != 48000)
        return fail("cannon clip format");
    if (cannon.frames < 4800 || cannon.frames > 48000)
        return fail("cannon clip length");
    const float cannonPeak = peakAbs(cannon.samples, cannon.frames);
    if (cannonPeak < 0.85f || cannonPeak > 0.91f)
        return fail("cannon peak " + std::to_string(cannonPeak));
    const int head = cannon.frames / 8;
    const int tail = cannon.frames / 8;
    const float headR = rms(cannon.samples, head);
    const float tailR = rms(cannon.samples + (cannon.frames - tail), tail);
    if (!(headR > tailR * 4.0f))
        return fail("cannon did not decay (head " + std::to_string(headR) + " tail " + std::to_string(tailR) + ")");

    if (bed.samples == nullptr || bed.channels != 2 || bed.sampleRate != 48000)
        return fail("bed clip format");
    if (bed.frames != 4 * 48000)
        return fail("bed clip length");
    const int bedSamples = bed.frames * bed.channels;
    const float bedPeak = peakAbs(bed.samples, bedSamples);
    const float bedR = rms(bed.samples, bedSamples);
    if (bedPeak < 0.30f || bedPeak > 0.41f)
        return fail("bed peak " + std::to_string(bedPeak));
    if (bedR < 0.02f)
        return fail("bed is silent (rms " + std::to_string(bedR) + ")");

    float maxStep = 0.0f;
    for (int i = 1; i < bed.frames; ++i) {
        for (int c = 0; c < 2; ++c) {
            const float d = std::fabs(bed.samples[i * 2 + c] - bed.samples[(i - 1) * 2 + c]);
            if (d > maxStep) maxStep = d;
        }
    }
    float seam = 0.0f;
    for (int c = 0; c < 2; ++c) {
        const float d = std::fabs(bed.samples[c] - bed.samples[(bed.frames - 1) * 2 + c]);
        if (d > seam) seam = d;
    }
    std::printf("audio_headless: bed seam %.5f max-step %.5f\n", seam, maxStep);
    std::fflush(stdout);
    if (seam > maxStep * 2.0f + 1.0e-4f)
        return fail("bed loop seam is a click");
    std::printf("audio_headless: clips ok (cannon peak %.3f, bed peak %.3f rms %.3f)\n",
                cannonPeak, bedPeak, bedR);
    std::fflush(stdout);
    return 0;
}

int exercise(bool expectMix) {
    audio::setBedEnabled(true);
    audio::update(0.05f, 0.5f);
    const float bedPeak = audio::lastNullPeak();
    audio::playOneShot(audio::OneShot::Cannon);
    audio::update(0.02f, 0.5f);
    const float shotPeak = audio::lastNullPeak();
    audio::setMuted(true);
    audio::playOneShot(audio::OneShot::Cannon);
    audio::update(0.03f, 0.5f);
    const float mutePeak = audio::lastNullPeak();
    audio::setMuted(false);
    audio::update(0.05f, 0.5f);
    const float unmutePeak = audio::lastNullPeak();
    std::printf("audio_headless: peaks bed %.4f shot %.4f mute %.6f unmute %.4f\n",
                bedPeak, shotPeak, mutePeak, unmutePeak);
    std::fflush(stdout);
    if (!expectMix) return 0;
    if (bedPeak < 0.01f) return fail("null bed pump was silent");
    if (shotPeak < 0.20f) return fail("null cannon pump was too quiet");
    if (shotPeak < bedPeak) return fail("cannon did not rise above the bed");
    if (mutePeak > 1.0e-4f) return fail("mute did not silence the null mix");
    if (unmutePeak < 0.01f) return fail("unmute did not restore the bed");
    return 0;
}

} // namespace

int main() {
    if (const int clips = checkClips()) return clips;

    if (!audio::init()) return fail("auto init");
    const audio::Status autoStatus = audio::status();
    if (!autoStatus.ready) return fail("auto init not ready");
    std::printf("audio_headless: auto device=%s\n", autoStatus.device ? "yes" : "no");
    std::fflush(stdout);
    if (const int rc = exercise(!autoStatus.device)) {
        audio::shutdown();
        return rc;
    }
    audio::shutdown();
    audio::playOneShot(audio::OneShot::Cannon); // after shutdown: must not crash
    audio::update(0.016f, 0.4f);

    audio::InitOptions nullOpt;
    nullOpt.forceNullDevice = true;
    if (!audio::init(nullOpt)) return fail("null init");
    const audio::Status nullStatus = audio::status();
    if (!nullStatus.ready || nullStatus.device) return fail("null init still has a device");
    if (const int rc = exercise(true)) {
        audio::shutdown();
        return rc;
    }
    audio::shutdown();
    audio::shutdown(); // idempotent
    std::printf("audio_headless: ok\n");
    return 0;
}
