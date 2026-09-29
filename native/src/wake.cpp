// Sea Trial — waterline foam / wake packing and bow spray. Public domain (Unlicense).
#include "wake.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace sea {

namespace {

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float saturate(float x) { return clampf(x, 0.0f, 1.0f); }

// HLSL/GLSL smoothstep, including inverted edges (the bow term uses them).
float smoothstep(float e0, float e1, float x) {
    const float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

float stepf(float edge, float x) { return x < edge ? 0.0f : 1.0f; }

// How far from this hull the foam can still show. Under way the V-wake reaches
// well astern; a still hull only needs the waterline ring. Must match fs_water.sc.
float wakeReach(float halfLen, float speed01) {
    if (speed01 > 0.01f) return halfLen * 2.0f + 160.0f;
    return halfLen * 2.5f + 6.0f;
}

// One hull. (px, pz) are scene XZ relative to the hull. Must match hullFoam()
// in shaders/fs_water.sc — same constants, including the scrolling V.
float hullFoam(float px, float pz, float sH, float cH, float hl, float hb,
               float spd, float timeSec) {
    const float slon = px * sH + pz * cH;
    const float slat = px * cH - pz * sH;
    float foam = 0.0f;

    const float e = (slat / hb) * (slat / hb) + (slon / hl) * (slon / hl);
    const float ring = (1.0f - smoothstep(0.0f, 0.5f, std::fabs(e - 1.0f))) * smoothstep(2.4f, 1.0f, e);
    foam += ring * 0.75f;

    const float bowT = smoothstep(hl * 1.8f, hl * 0.7f, slon) * stepf(hl * 0.35f, slon);
    foam += bowT * smoothstep(hb * 1.4f, 0.0f, std::fabs(slat)) * spd * 0.9f;

    const float wd = -slon - hl * 0.5f;
    if (wd > 0.0f) {
        const float arm = hb * 0.6f + wd * 0.34f;
        const float d = (std::fabs(slat) - arm) / (hb * 0.55f);
        const float vfoam = std::exp(-(d * d));
        const float scroll = 0.75f + 0.25f * std::sin(wd * 0.35f - timeSec * 1.7f);
        foam += vfoam * std::exp(-wd * 0.02f) * spd * 0.8f * scroll;
        foam += smoothstep(hb * 0.9f, 0.0f, std::fabs(slat)) * std::exp(-wd * 0.06f) * spd * 0.5f;
    }
    return foam;
}

} // namespace

float wakeSpeed01(float speedMps) {
    if (speedMps <= 0.0f) return 0.0f;
    return clampf(speedMps / kWakeFullSpeed, 0.0f, 1.0f);
}

WakeHull makeWake(float headingRad, float length, float beam, float speedMps,
                  float sceneX, float sceneZ, float baseY) {
    WakeHull h;
    h.sinH = std::sin(headingRad);
    h.cosH = std::cos(headingRad);
    h.halfLen = length * 0.5f;
    h.halfBeam = beam * 0.6f;
    h.speed01 = wakeSpeed01(speedMps);
    h.sceneX = sceneX;
    h.sceneZ = sceneZ;
    h.baseY = baseY;
    return h;
}

int packWakes(const WakeHull* hulls, int hullCount, WakePack& out) {
    for (int i = 0; i < kMaxWakeHulls; ++i) {
        for (int k = 0; k < 4; ++k) {
            out.ship[i][k] = 0.0f;
            out.dyn[i][k] = 0.0f;
        }
    }
    out.count = 0;
    if (!hulls || hullCount <= 0) return 0;
    for (int i = 0; i < hullCount && out.count < kMaxWakeHulls; ++i) {
        const WakeHull& h = hulls[i];
        if (h.halfLen < 0.1f || h.halfBeam < 0.05f) continue;
        const int s = out.count++;
        out.ship[s][0] = h.sinH;
        out.ship[s][1] = h.cosH;
        out.ship[s][2] = h.halfLen;
        out.ship[s][3] = h.halfBeam;
        out.dyn[s][0] = clampf(h.speed01, 0.0f, 1.0f);
        out.dyn[s][1] = h.sceneX;
        out.dyn[s][2] = h.sceneZ;
    }
    return out.count;
}

float wakeFoamAt(const WakePack& pack, float sceneX, float sceneZ, float timeSec) {
    float sum = 0.0f;
    for (int i = 0; i < kMaxWakeHulls; ++i) {
        const float hl = pack.ship[i][2];
        const float hb = pack.ship[i][3];
        if (hl < 0.1f || hb < 0.05f) continue;
        const float spd = pack.dyn[i][0];
        const float px = sceneX - pack.dyn[i][1];
        const float pz = sceneZ - pack.dyn[i][2];
        const float reach = wakeReach(hl, spd);
        if (px * px + pz * pz > reach * reach) continue;
        sum += hullFoam(px, pz, pack.ship[i][0], pack.ship[i][1], hl, hb, spd, timeSec);
    }
    return clampf(sum, 0.0f, 1.0f);
}

int harbourMoorings(float islandSceneX, float islandSceneZ, WakeHull* out, int cap) {
    if (!out || cap <= 0) return 0;
    int n = 0;
    for (int i = 0; i < kHarbourMooringCount && n < cap; ++i) {
        const MoorSpec& m = kHarbourMoorings[i];
        out[n++] = makeWake(m.heading, m.length, m.beam, 0.0f,
                            islandSceneX + m.localX, islandSceneZ + m.localZ, 0.0f);
    }
    return n;
}

int sampleBowSpray(const WakeHull& h, float timeSec, SprayDrop* out, int cap) {
    if (!out || cap <= 0) return 0;
    if (h.halfLen < 0.1f || h.halfBeam < 0.05f) return 0;
    if (!(h.speed01 >= kBowSprayMinSpeed)) return 0;

    const int n = cap < kBowSprayPerHull ? cap : kBowSprayPerHull;
    const float t = saturate((h.speed01 - kBowSprayMinSpeed) / (1.0f - kBowSprayMinSpeed));
    int count = 4 + int(std::lround(t * float(kBowSprayPerHull - 4)));
    if (count > n) count = n;

    const float reach = 0.6f + 2.6f * clampf(h.speed01, 0.0f, 1.0f);
    for (int i = 0; i < count; ++i) {
        const float rate = 1.35f + 0.17f * float(i % 5);
        float phase = std::fmod(timeSec * rate + float(i) * 0.173f, 1.0f);
        if (phase < 0.0f) phase += 1.0f;
        const float side = (i % 2 == 0) ? 1.0f : -1.0f;
        const float along = h.halfLen + 0.35f + phase * reach;
        const float lat = side * (h.halfBeam * 0.15f + phase * h.halfBeam * 1.05f);
        const float arc = std::sin(phase * 3.14159265f);
        SprayDrop& d = out[i];
        d.x = h.sceneX + along * h.sinH + lat * h.cosH;
        d.y = h.baseY + 0.28f + arc * (0.55f + 1.25f * clampf(h.speed01, 0.0f, 1.0f));
        d.z = h.sceneZ + along * h.cosH - lat * h.sinH;
        d.size = 0.28f + 0.40f * arc * clampf(h.speed01, 0.0f, 1.0f);
    }
    return count;
}

std::vector<TestResult> runWakeSelfTest() {
    std::vector<TestResult> r;
    auto push = [&](const std::string& name, bool pass, const std::string& details = "") {
        r.push_back({ name, pass, details });
    };
    auto num = [](float v) {
        char b[48];
        std::snprintf(b, sizeof(b), "%.3f", double(v));
        return std::string(b);
    };

    push("Wake speed is 0 at rest and in reverse",
         wakeSpeed01(0.0f) == 0.0f && wakeSpeed01(-4.0f) == 0.0f);
    push("Wake speed is half at 3.5 m/s and full at 7",
         std::fabs(wakeSpeed01(3.5f) - 0.5f) < 1e-5f && wakeSpeed01(7.0f) == 1.0f
             && wakeSpeed01(16.5f) == 1.0f);

    const WakeHull player = makeWake(0.0f, 12.0f, 4.0f, 7.0f, 0.0f, 0.0f, 0.4f);
    push("Player footprint matches the old waterline scale",
         std::fabs(player.halfLen - 6.0f) < 1e-5f && std::fabs(player.halfBeam - 2.4f) < 1e-5f
             && player.speed01 == 1.0f && player.sinH == 0.0f && player.cosH == 1.0f,
         "half " + num(player.halfLen) + " x " + num(player.halfBeam));

    WakeHull list[12];
    list[0] = player;
    list[1] = makeWake(1.5707963f, 16.0f, 5.0f, 3.5f, 18.0f, -6.0f, 0.2f);
    list[2] = makeWake(0.0f, 0.0f, 4.0f, 5.0f, 3.0f, 3.0f); // no footprint, skipped
    list[2].halfLen = 0.0f;
    WakePack pack;
    const int packed = packWakes(list, 3, pack);
    const bool slot0 = pack.ship[0][2] == player.halfLen && pack.dyn[0][1] == 0.0f && pack.dyn[0][2] == 0.0f
                       && std::fabs(pack.dyn[0][0] - 1.0f) < 1e-5f;
    const bool slot1 = std::fabs(pack.dyn[1][1] - 18.0f) < 1e-4f && std::fabs(pack.dyn[1][2] - (-6.0f)) < 1e-4f
                       && std::fabs(pack.dyn[1][0] - 0.5f) < 1e-4f && pack.ship[1][2] > 7.0f;
    const bool tail = pack.ship[2][2] == 0.0f && pack.ship[kMaxWakeHulls - 1][2] == 0.0f;
    push("Pack keeps the player, an offset enemy, and skips an empty hull",
         packed == 2 && slot0 && slot1 && tail,
         "count " + num(float(packed)));

    WakeHull tooMany[kMaxWakeHulls + 1];
    for (int i = 0; i < kMaxWakeHulls + 1; ++i)
        tooMany[i] = makeWake(0.0f, 10.0f, 4.0f, 0.0f, float(i) * 10.0f, 0.0f);
    WakePack capped;
    const int capN = packWakes(tooMany, kMaxWakeHulls + 1, capped);
    push("Pack stops at the shader hull cap",
         capN == kMaxWakeHulls && capped.dyn[kMaxWakeHulls - 1][1] == float(kMaxWakeHulls - 1) * 10.0f
             && capped.count == kMaxWakeHulls);

    WakeHull hot = player;
    hot.speed01 = 4.0f;
    WakePack clamped;
    packWakes(&hot, 1, clamped);
    push("Packed speed stays inside 0..1", clamped.dyn[0][0] == 1.0f);

    // Heading 0: +Z is forward, +X is to starboard (slat).
    auto foam = [&](float px, float pz, float spd, float time) {
        WakeHull h = makeWake(0.0f, 12.0f, 4.0f, 0.0f, 0.0f, 0.0f);
        h.speed01 = spd;
        WakePack p;
        packWakes(&h, 1, p);
        return wakeFoamAt(p, px, pz, time);
    };

    const float ring = foam(0.0f, 6.0f, 0.0f, 0.0f); // on the bow of the ellipse
    const float far = foam(0.0f, 80.0f, 0.0f, 0.0f);
    push("Still hull has a waterline ring and no foam far ahead",
         ring > 0.6f && far < 0.05f, "ring " + num(ring) + " far " + num(far));

    const float bowRest = foam(0.0f, 9.0f, 0.0f, 0.0f);
    const float bowWay = foam(0.0f, 9.0f, 1.0f, 0.0f);
    push("Bow wave ahead of the stem strengthens with speed",
         bowWay > bowRest + 0.08f && bowRest < 0.05f,
         "rest " + num(bowRest) + " way " + num(bowWay));

    const float hl = 6.0f, hb = 2.4f;
    const float slon = -10.0f;
    const float wd = -slon - hl * 0.5f;
    const float arm = hb * 0.6f + wd * 0.34f;
    const float wakeRest = foam(arm, slon, 0.0f, 0.0f);
    const float wakeWay = foam(arm, slon, 1.0f, 0.0f);
    push("V-wake sits astern only when there is way on",
         wakeWay > 0.30f && wakeRest < 0.05f, "rest " + num(wakeRest) + " way " + num(wakeWay));

    const float wakeLater = foam(arm, slon, 1.0f, 2.365f);
    push("V-wake scrolls with time",
         std::fabs(wakeWay - wakeLater) > 0.02f, num(wakeWay) + " -> " + num(wakeLater));

    const float interior = foam(0.0f, -20.0f, 1.0f, 0.0f);
    push("Wake interior stays a finite foam value",
         interior >= 0.0f && interior <= 1.0f, num(interior));

    WakeHull both[2];
    both[0] = makeWake(0.0f, 12.0f, 4.0f, 0.0f, 0.0f, 0.0f);
    both[1] = makeWake(0.0f, 12.0f, 4.0f, 0.0f, 50.0f, 10.0f);
    WakePack two;
    packWakes(both, 2, two);
    const float own = wakeFoamAt(two, 0.0f, 6.0f, 0.0f);
    const float other = wakeFoamAt(two, 50.0f, 16.0f, 0.0f);
    const float gap = wakeFoamAt(two, 25.0f, 10.0f, 0.0f);
    push("A second hull foams at its own waterline",
         own > 0.6f && other > 0.6f && gap < 0.05f,
         "own " + num(own) + " other " + num(other) + " gap " + num(gap));

    WakeHull distant = makeWake(0.0f, 12.0f, 4.0f, 0.0f, 300.0f, 0.0f);
    WakePack farPack;
    packWakes(&distant, 1, farPack);
    const float farRing = wakeFoamAt(farPack, 300.0f, 6.0f, 0.0f);
    push("A distant hull still gets its own ring", farRing > 0.6f, num(farRing));

    WakeHull moor[4];
    const int mn = harbourMoorings(10.0f, 20.0f, moor, 4);
    const int m0 = harbourMoorings(0.0f, 0.0f, moor, 0);
    WakeHull one[1];
    const int m1 = harbourMoorings(0.0f, 0.0f, one, 1);
    const bool boat0 = std::fabs(moor[0].sceneX - (10.0f - 32.0f)) < 1e-3f
                       && std::fabs(moor[0].sceneZ - (20.0f - 50.0f)) < 1e-3f
                       && moor[0].speed01 == 0.0f
                       && std::fabs(moor[0].halfBeam - 2.6f * 0.6f) < 1e-4f;
    const bool galleon = mn >= 3 && std::fabs(moor[2].halfLen - 11.0f) < 1e-3f && moor[2].cosH < 0.0f
                         && std::fabs(moor[2].sinH) < 1e-4f && moor[2].speed01 == 0.0f;
    push("Harbour moorings are still hulls placed on the island",
         mn == kHarbourMooringCount && m0 == 0 && m1 == 1 && boat0 && galleon,
         "count " + num(float(mn)));

    WakePack moorPack;
    // Re-read at island origin so the galleon sits on its chart position.
    WakeHull chart[4];
    harbourMoorings(0.0f, 0.0f, chart, 4);
    packWakes(chart, kHarbourMooringCount, moorPack);
    // Side of the galleon ellipse (heading pi, cos ≈ -1): slat = +halfBeam -> world X negative.
    const float gBeam = 6.0f * 0.6f;
    const float gRing = wakeFoamAt(moorPack, 31.0f + gBeam * chart[2].cosH, -56.0f, 0.0f);
    const float gAstern = wakeFoamAt(moorPack, 31.0f, -36.0f, 0.0f);
    push("Moored galleon has a waterline ring and no wake",
         gRing > 0.6f && gAstern < 0.05f, "ring " + num(gRing) + " astern " + num(gAstern));

    SprayDrop drops[kBowSprayPerHull];
    WakeHull slow = makeWake(0.0f, 12.0f, 4.0f, 0.0f, 0.0f, 0.0f);
    slow.speed01 = 0.219f;
    push("Bow spray is off below the speed gate",
         sampleBowSpray(slow, 1.0f, drops, kBowSprayPerHull) == 0
             && sampleBowSpray(player, 1.0f, nullptr, 4) == 0
             && sampleBowSpray(player, 1.0f, drops, 0) == 0);

    WakeHull gated = player;
    gated.speed01 = kBowSprayMinSpeed;
    const int nGate = sampleBowSpray(gated, 0.0f, drops, kBowSprayPerHull);
    const int nFull = sampleBowSpray(player, 0.0f, drops, kBowSprayPerHull);
    const int nCap = sampleBowSpray(player, 0.0f, drops, 3);
    push("Bow spray count follows speed and the buffer cap",
         nGate == 4 && nFull == kBowSprayPerHull && nCap == 3,
         "gate " + num(float(nGate)) + " full " + num(float(nFull)) + " cap " + num(float(nCap)));

    WakeHull thrown = makeWake(1.7f, 14.0f, 5.0f, 10.0f, 12.0f, -4.0f, 0.5f);
    SprayDrop arc[kBowSprayPerHull];
    const int nArc = sampleBowSpray(thrown, 0.8f, arc, kBowSprayPerHull);
    bool ahead = nArc == kBowSprayPerHull;
    float minY = 1.0e9f, maxY = -1.0e9f;
    for (int i = 0; i < nArc; ++i) {
        const float dx = arc[i].x - thrown.sceneX;
        const float dz = arc[i].z - thrown.sceneZ;
        const float sl = dx * thrown.sinH + dz * thrown.cosH;
        const float sa = dx * thrown.cosH - dz * thrown.sinH;
        if (!(sl > thrown.halfLen)) ahead = false;
        if (!(std::fabs(sa) < thrown.halfBeam * 2.5f)) ahead = false;
        if (arc[i].y < minY) minY = arc[i].y;
        if (arc[i].y > maxY) maxY = arc[i].y;
        if (!(arc[i].size > 0.0f)) ahead = false;
    }
    push("Bow spray stays ahead of the stem and above the waterline",
         ahead && minY >= thrown.baseY && maxY > thrown.baseY + 0.5f,
         "n " + num(float(nArc)) + " y " + num(minY) + ".." + num(maxY));

    SprayDrop a[kBowSprayPerHull], b[kBowSprayPerHull], c[kBowSprayPerHull];
    const int na = sampleBowSpray(player, 1.25f, a, kBowSprayPerHull);
    const int nb = sampleBowSpray(player, 1.25f, b, kBowSprayPerHull);
    const int nc = sampleBowSpray(player, 1.25f + 0.4f, c, kBowSprayPerHull);
    bool same = na == nb && na > 0;
    bool moved = na == nc && na > 0;
    for (int i = 0; i < na; ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z || a[i].size != b[i].size)
            same = false;
        if (a[i].x == c[i].x && a[i].y == c[i].y && a[i].z == c[i].z) moved = false;
    }
    push("Bow spray is deterministic and moves with time", same && moved);

    push("Spray budget covers the player and one enemy",
         kMaxBowSprayDrops >= 2 * kBowSprayPerHull);

    return r;
}

} // namespace sea
