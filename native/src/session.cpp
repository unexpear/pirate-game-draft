// Sea Trial — Phase 8 two-player sailing session. Public domain (The Unlicense).
#include "session.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sea {

namespace {

constexpr double kTau = 6.28318530717958647692;
constexpr double kPi = 3.14159265358979323846;
constexpr double kClockSnap = 0.25;
constexpr int kTrackCap = 16;
constexpr int kOutboxCap = 32;
constexpr int kMaxPieces = 4096;

std::string d2s(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

bool readFinite(std::istream& in, double& v) {
    if (!(in >> v)) return false;
    return std::isfinite(v);
}

void enqueue(std::vector<std::string>& outbox, std::string msg) {
    if (static_cast<int>(outbox.size()) >= kOutboxCap) outbox.erase(outbox.begin());
    outbox.push_back(std::move(msg));
}

double wrapPi(double d) {
    while (d > kPi) d -= kTau;
    while (d < -kPi) d += kTau;
    return d;
}

double lerpAngle(double a, double b, double u) {
    return a + wrapPi(b - a) * u;
}

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kBadSocket = INVALID_SOCKET;
void closeSocket(SocketHandle s) { closesocket(s); }
bool startupSockets() {
    static bool ready = false;
    if (ready) return true;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    ready = true;
    return true;
}
#else
using SocketHandle = int;
constexpr SocketHandle kBadSocket = -1;
void closeSocket(SocketHandle s) { ::close(s); }
bool startupSockets() { return true; }
#endif

bool setNonBlocking(SocketHandle s) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool samePeer(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

bool parseIPv4(const std::string& host, int port, sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<uint16_t>(port));
    const std::string addr = (host == "localhost") ? "127.0.0.1" : host;
    return inet_pton(AF_INET, addr.c_str(), &out.sin_addr) == 1;
}

} // namespace

double sessionWindDir(double windBase, double windRate, double sessionTime) {
    double w = std::fmod(windBase + windRate * sessionTime, kTau);
    if (w < 0) w += kTau;
    return w;
}

std::string encodeSnapshot(const ShipSnapshot& s) {
    std::string o = "SEA1 snap " + std::to_string(s.playerId) + " " + d2s(s.sessionTime) + " " +
                    d2s(s.windDir) + " " + d2s(s.x) + " " + d2s(s.z) + " " + d2s(s.heading) + " " +
                    d2s(s.speed) + " " + std::to_string(s.sailTier) + " " + d2s(s.cargoMass) + " " +
                    d2s(s.sinkDepth) + " " + d2s(s.heaveY) + " " + d2s(s.pitch) + " " + d2s(s.heel) + " " +
                    std::to_string(s.pieceDamage.size());
    for (double d : s.pieceDamage) o += " " + d2s(d);
    return o;
}

bool decodeSnapshot(const std::string& text, ShipSnapshot& snap) {
    std::istringstream in(text);
    std::string magic, verb;
    if (!(in >> magic >> verb)) return false;
    if (magic != "SEA1" || verb != "snap") return false;
    ShipSnapshot s;
    int n = 0;
    if (!(in >> s.playerId)) return false;
    if (!readFinite(in, s.sessionTime) || !readFinite(in, s.windDir)) return false;
    if (!readFinite(in, s.x) || !readFinite(in, s.z) || !readFinite(in, s.heading)) return false;
    if (!readFinite(in, s.speed)) return false;
    if (!(in >> s.sailTier)) return false;
    if (!readFinite(in, s.cargoMass) || !readFinite(in, s.sinkDepth)) return false;
    if (!readFinite(in, s.heaveY) || !readFinite(in, s.pitch) || !readFinite(in, s.heel)) return false;
    if (!(in >> n) || n < 0 || n > kMaxPieces) return false;
    s.pieceDamage.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        if (!readFinite(in, s.pieceDamage[static_cast<size_t>(i)])) return false;
    snap = std::move(s);
    return true;
}

std::string encodeDamage(const DamageEvent& ev) {
    return "SEA1 dmg " + std::to_string(ev.playerId) + " " + std::to_string(ev.pieceIndex) + " " +
           d2s(ev.damage) + " " + d2s(ev.sessionTime);
}

bool decodeDamage(const std::string& text, DamageEvent& ev) {
    std::istringstream in(text);
    std::string magic, verb;
    if (!(in >> magic >> verb)) return false;
    if (magic != "SEA1" || verb != "dmg") return false;
    DamageEvent e;
    if (!(in >> e.playerId >> e.pieceIndex)) return false;
    if (!readFinite(in, e.damage) || !readFinite(in, e.sessionTime)) return false;
    ev = e;
    return true;
}

std::string encodeWelcome(const SessionParams& params, int assignedId, double sessionTime) {
    return "SEA1 welcome " + std::to_string(params.protocol) + " " + params.waveSeed + " " +
           d2s(params.windBase) + " " + d2s(params.windRate) + " " + std::to_string(assignedId) + " " +
           d2s(sessionTime);
}

bool decodeWelcome(const std::string& text, SessionParams& params, int& assignedId, double& sessionTime) {
    std::istringstream in(text);
    std::string magic, verb, seed;
    if (!(in >> magic >> verb >> params.protocol >> seed)) return false;
    if (magic != "SEA1" || verb != "welcome") return false;
    if (seed.empty()) return false;
    params.waveSeed = seed;
    if (!readFinite(in, params.windBase) || !readFinite(in, params.windRate)) return false;
    if (!(in >> assignedId)) return false;
    if (!readFinite(in, sessionTime)) return false;
    return true;
}

std::string encodeHello(const std::string& name) {
    const std::string token = name.empty() ? "helm" : name;
    return "SEA1 hello " + token;
}

bool decodeHello(const std::string& text, std::string& name) {
    std::istringstream in(text);
    std::string magic, verb;
    if (!(in >> magic >> verb >> name)) return false;
    return magic == "SEA1" && verb == "hello" && !name.empty();
}

void applySnapshot(Ship& ship, const ShipSnapshot& snap) {
    ship.systems.cargo_mass = snap.cargoMass;
    const size_t n = ship.pieces.size() < snap.pieceDamage.size() ? ship.pieces.size() : snap.pieceDamage.size();
    for (size_t i = 0; i < n; ++i) {
        double d = snap.pieceDamage[i];
        if (d < 0) d = 0;
        if (d > 1) d = 1;
        ship.pieces[i].damage = d;
    }
}

void fillSnapshotDamage(ShipSnapshot& snap, const Ship& ship) {
    snap.cargoMass = ship.systems.cargo_mass;
    snap.pieceDamage.resize(ship.pieces.size());
    for (size_t i = 0; i < ship.pieces.size(); ++i) snap.pieceDamage[i] = ship.pieces[i].damage;
}

bool applyDamageEvent(Ship& ship, const DamageEvent& ev) {
    if (ev.pieceIndex < 0) {
        for (auto& p : ship.pieces) p.damage = 0;
        return true;
    }
    if (ev.pieceIndex >= static_cast<int>(ship.pieces.size())) return false;
    double d = ev.damage;
    if (d < 0) d = 0;
    if (d > 1) d = 1;
    ship.pieces[static_cast<size_t>(ev.pieceIndex)].damage = d;
    return true;
}

FloatPose poseForSnapshot(const Ship& prototype, const std::vector<Wave>& waves, const ShipSnapshot& snap) {
    Ship s = prototype;
    applySnapshot(s, snap);
    FloatPose p = computeFloatPose(s, waves, snap.sessionTime, snap.x, snap.z, snap.heading);
    p.heaveY -= snap.sinkDepth;
    return p;
}

bool posesAgree(const FloatPose& a, const FloatPose& b, double tolerance) {
    return std::fabs(a.heaveY - b.heaveY) <= tolerance &&
           std::fabs(a.pitch - b.pitch) <= tolerance &&
           std::fabs(a.heel - b.heel) <= tolerance;
}

void NetClock::observe(double hostTime, double localTime) {
    const double err = hostTime - (localTime + offset);
    if (!synced || std::fabs(err) > kClockSnap) {
        offset = hostTime - localTime;
        synced = true;
    }
}

double NetClock::now(double localTime) const {
    return synced ? localTime + offset : localTime;
}

void RemoteTrack::push(const ShipSnapshot& snap) {
    if (!snaps.empty()) {
        const double last = snaps.back().sessionTime;
        if (snap.sessionTime < last - 1e-9) return;
        if (std::fabs(snap.sessionTime - last) <= 1e-9) {
            snaps.back() = snap;
            return;
        }
    }
    snaps.push_back(snap);
    if (static_cast<int>(snaps.size()) > kTrackCap) snaps.erase(snaps.begin());
}

bool RemoteTrack::sample(double sessionTime, InterpShip& out) const {
    if (snaps.empty()) return false;
    auto hold = [&](const ShipSnapshot& s) {
        out.x = s.x;
        out.z = s.z;
        out.heading = s.heading;
        out.speed = s.speed;
        out.heaveY = s.heaveY;
        out.pitch = s.pitch;
        out.heel = s.heel;
        out.sinkDepth = s.sinkDepth;
        out.sailTier = s.sailTier;
        out.cargoMass = s.cargoMass;
        out.sessionTime = s.sessionTime;
    };
    if (snaps.size() == 1 || sessionTime <= snaps.front().sessionTime) {
        hold(snaps.front());
        return true;
    }
    if (sessionTime >= snaps.back().sessionTime) {
        hold(snaps.back());
        return true;
    }
    size_t i = 1;
    while (i + 1 < snaps.size() && snaps[i].sessionTime < sessionTime) ++i;
    const ShipSnapshot& a = snaps[i - 1];
    const ShipSnapshot& b = snaps[i];
    const double span = b.sessionTime - a.sessionTime;
    const double u = span > 1e-9 ? (sessionTime - a.sessionTime) / span : 1.0;
    out.x = a.x + (b.x - a.x) * u;
    out.z = a.z + (b.z - a.z) * u;
    out.heading = lerpAngle(a.heading, b.heading, u);
    out.speed = a.speed + (b.speed - a.speed) * u;
    out.heaveY = a.heaveY + (b.heaveY - a.heaveY) * u;
    out.pitch = a.pitch + (b.pitch - a.pitch) * u;
    out.heel = a.heel + (b.heel - a.heel) * u;
    out.sinkDepth = a.sinkDepth + (b.sinkDepth - a.sinkDepth) * u;
    out.sailTier = u >= 1.0 ? b.sailTier : a.sailTier;
    out.cargoMass = u >= 1.0 ? b.cargoMass : a.cargoMass;
    out.sessionTime = sessionTime;
    return true;
}

double RemoteTrack::newestTime() const {
    return snaps.empty() ? 0.0 : snaps.back().sessionTime;
}

SailSession SailSession::host(const std::string& waveSeed, double windBase, double windRate) {
    SailSession s;
    s.role = SessionRole::Host;
    s.localPlayerId = 0;
    s.params.protocol = kSessionProtocol;
    s.params.waveSeed = waveSeed;
    s.params.windBase = windBase;
    s.params.windRate = windRate;
    return s;
}

SailSession SailSession::client() {
    SailSession s;
    s.role = SessionRole::Client;
    s.localPlayerId = -1;
    s.joined = false;
    return s;
}

void SailSession::setHostClock(double sessionTime) { hostNow = sessionTime; }

void SailSession::queueHello(const std::string& name) { enqueue(outbox, encodeHello(name)); }

void SailSession::publish(const ShipSnapshot& snap) { enqueue(outbox, encodeSnapshot(snap)); }

void SailSession::publishDamage(const DamageEvent& ev) { enqueue(outbox, encodeDamage(ev)); }

void SailSession::receive(const std::string& datagram) {
    std::istringstream in(datagram);
    std::string magic, verb;
    if (!(in >> magic >> verb) || magic != "SEA1") return;

    if (verb == "hello") {
        if (role != SessionRole::Host || joined) return;
        std::string name;
        in >> name;
        if (name.empty()) name = "helm";
        peerName = name;
        joined = true;
        enqueue(outbox, encodeWelcome(params, 1, hostNow));
        return;
    }
    if (verb == "welcome") {
        if (role != SessionRole::Client || joined) return;
        SessionParams got;
        int assigned = -1;
        double t = 0;
        if (!decodeWelcome(datagram, got, assigned, t)) return;
        if (got.protocol != kSessionProtocol) return;
        params = got;
        localPlayerId = assigned;
        hostTime = t;
        ++hostTimeGen;
        joined = true;
        peerName = "host";
        return;
    }
    if (verb == "snap") {
        ShipSnapshot snap;
        if (!decodeSnapshot(datagram, snap)) return;
        if (snap.playerId == localPlayerId) return;
        remote.push(snap);
        latestRemote = snap;
        hasRemote = true;
        if (snap.playerId == 0) {
            hostTime = snap.sessionTime;
            ++hostTimeGen;
        }
        return;
    }
    if (verb == "dmg") {
        DamageEvent ev;
        if (!decodeDamage(datagram, ev)) return;
        if (ev.playerId == localPlayerId) return;
        lastDamage = ev;
        hasDamage = true;
    }
}

std::vector<std::string> SailSession::takeOutbound() {
    std::vector<std::string> out;
    out.swap(outbox);
    return out;
}

struct UdpSocket::Impl {
    SocketHandle fd = kBadSocket;
    int port = 0;
    bool open = false;
    bool hasPeer = false;
    sockaddr_in peer{};
};

UdpSocket::UdpSocket() : impl(new Impl) {}

UdpSocket::~UdpSocket() {
    close();
    delete impl;
    impl = nullptr;
}

bool UdpSocket::bind(const std::string& host, int port) {
    close();
    if (!startupSockets()) return false;
    impl->fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (impl->fd == kBadSocket) return false;
    if (!setNonBlocking(impl->fd)) {
        close();
        return false;
    }
    const int yes = 1;
    ::setsockopt(impl->fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port < 0 ? 0 : port));
    if (host.empty() || host == "0.0.0.0") addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else if (!parseIPv4(host, port, addr)) {
        close();
        return false;
    }
    if (::bind(impl->fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close();
        return false;
    }
    sockaddr_in actual{};
    socklen_t len = sizeof(actual);
    if (getsockname(impl->fd, reinterpret_cast<sockaddr*>(&actual), &len) == 0)
        impl->port = ntohs(actual.sin_port);
    impl->open = true;
    return true;
}

bool UdpSocket::setPeer(const std::string& host, int port) {
    if (!impl->open) return false;
    if (!parseIPv4(host, port, impl->peer)) return false;
    impl->hasPeer = true;
    return true;
}

bool UdpSocket::send(const std::string& payload) {
    if (!impl->open || !impl->hasPeer || payload.empty()) return false;
    const int n = static_cast<int>(payload.size());
    const int sent = ::sendto(impl->fd, payload.data(), n, 0,
                              reinterpret_cast<sockaddr*>(&impl->peer), sizeof(impl->peer));
    return sent == n;
}

bool UdpSocket::recv(std::string& payload) {
    if (!impl->open) return false;
    char buf[65536];
    sockaddr_in from{};
    socklen_t flen = sizeof(from);
    const int n = ::recvfrom(impl->fd, buf, static_cast<int>(sizeof(buf)), 0,
                             reinterpret_cast<sockaddr*>(&from), &flen);
    if (n < 0) return false;
    if (n == 0) return false;
    if (!impl->hasPeer) {
        impl->peer = from;
        impl->hasPeer = true;
    } else if (!samePeer(from, impl->peer)) {
        return false;
    }
    payload.assign(buf, buf + n);
    return true;
}

void UdpSocket::close() {
    if (impl == nullptr) return;
    if (impl->fd != kBadSocket) closeSocket(impl->fd);
    impl->fd = kBadSocket;
    impl->open = false;
    impl->hasPeer = false;
    impl->port = 0;
}

int UdpSocket::boundPort() const { return impl->port; }

bool UdpSocket::isOpen() const { return impl->open; }

std::vector<TestResult> runSessionSelfTest() {
    std::vector<TestResult> r;
    auto push = [&](const std::string& name, bool pass, const std::string& details = "") {
        r.push_back({ name, pass, details });
    };
    auto near = [](double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; };

    ShipConfig cfg;
    cfg.name = "Session Sloop";
    const Ship prototype = makeShipFromConfig(cfg);

    ShipSnapshot snap;
    snap.playerId = 0;
    snap.sessionTime = 4.5;
    snap.windDir = sessionWindDir(2.1, 0.02, snap.sessionTime);
    snap.x = 30.0;
    snap.z = -12.5;
    snap.heading = 0.7;
    snap.speed = 6.25;
    snap.sailTier = 2;
    snap.cargoMass = 400.0;
    snap.sinkDepth = 0.4;
    snap.pieceDamage.assign(prototype.pieces.size(), 0.0);
    if (snap.pieceDamage.size() > 5) snap.pieceDamage[5] = 0.5;
    const auto waves = makeWaveField("phase8-seed");
    const FloatPose posed = poseForSnapshot(prototype, waves, snap);
    snap.heaveY = posed.heaveY;
    snap.pitch = posed.pitch;
    snap.heel = posed.heel;

    ShipSnapshot decoded;
    const std::string snapText = encodeSnapshot(snap);
    const bool snapOk = decodeSnapshot(snapText, decoded) && decoded.playerId == 0 &&
                        near(decoded.sessionTime, snap.sessionTime) && near(decoded.x, snap.x) &&
                        near(decoded.z, snap.z) && near(decoded.heading, snap.heading) &&
                        decoded.sailTier == 2 && near(decoded.cargoMass, 400.0) &&
                        near(decoded.sinkDepth, 0.4) && decoded.pieceDamage.size() == snap.pieceDamage.size() &&
                        near(decoded.pieceDamage[5], 0.5) && encodeSnapshot(decoded) == snapText;
    push("Ship snapshot round-trips through encode/decode", snapOk, snapOk ? "" : "snapshot fields differ");

    DamageEvent dmg{ 0, 5, 0.75, 4.5 };
    DamageEvent dmgOut;
    const std::string dmgText = encodeDamage(dmg);
    const bool dmgOk = decodeDamage(dmgText, dmgOut) && dmgOut.playerId == 0 && dmgOut.pieceIndex == 5 &&
                       near(dmgOut.damage, 0.75) && near(dmgOut.sessionTime, 4.5);
    push("Damage event round-trips through encode/decode", dmgOk);

    SessionParams welcomeParams;
    welcomeParams.waveSeed = "phase8-seed";
    welcomeParams.windBase = 2.1;
    welcomeParams.windRate = 0.02;
    int assigned = -1;
    double welcomeTime = -1;
    SessionParams welcomeOut;
    const bool welcomeOk = decodeWelcome(encodeWelcome(welcomeParams, 1, 3.25), welcomeOut, assigned, welcomeTime) &&
                           welcomeOut.protocol == kSessionProtocol && welcomeOut.waveSeed == "phase8-seed" &&
                           near(welcomeOut.windBase, 2.1) && assigned == 1 && near(welcomeTime, 3.25);
    push("Welcome round-trips the wave seed and session clock", welcomeOk);

    ShipSnapshot junk;
    DamageEvent junkDmg;
    std::string junkName;
    SessionParams junkParams;
    int junkId = 0;
    double junkT = 0;
    const bool rejected = !decodeSnapshot("nope", junk) && !decodeSnapshot("SEA1 snap", junk) &&
                          !decodeDamage("SEA1 snap 1", junkDmg) && !decodeHello("SEA1 welcome x", junkName) &&
                          !decodeWelcome("SEA1 welcome 1", junkParams, junkId, junkT);
    push("Malformed session datagrams are rejected", rejected);

    SailSession badClient = SailSession::client();
    badClient.receive("SEA1 welcome 99 other-seed 2.1 0.02 1 0");
    badClient.receive("this is not a packet");
    push("A protocol mismatch does not join", !badClient.joined && !badClient.hasRemote);

    Ship applied = prototype;
    ShipSnapshot clampSnap = snap;
    clampSnap.pieceDamage[5] = 2.0;
    clampSnap.cargoMass = 250.0;
    applySnapshot(applied, clampSnap);
    const bool clamped = near(applied.systems.cargo_mass, 250.0) && near(applied.pieces[5].damage, 1.0);
    push("Applying a snapshot sets cargo and clamps piece damage", clamped);

    Ship damaged = prototype;
    const double before = getShipStats(damaged).buoyancyScore;
    DamageEvent flood{ 1, 5, 1.0, 1.0 };
    const bool appliedOnce = applyDamageEvent(damaged, flood);
    const double after = getShipStats(damaged).buoyancyScore;
    applyDamageEvent(damaged, flood);
    const double afterTwice = getShipStats(damaged).buoyancyScore;
    DamageEvent repair{ 1, -1, 0.0, 2.0 };
    applyDamageEvent(damaged, repair);
    const bool dmgEffect = appliedOnce && after < before && near(afterTwice, after) &&
                           near(getShipStats(damaged).buoyancyScore, before);
    push("Damage event lowers buoyancy and applying it twice is a no-op", dmgEffect);

    RemoteTrack track;
    ShipSnapshot a = snap;
    a.sessionTime = 0;
    a.x = 0;
    a.z = 0;
    a.heading = 6.0;
    a.sailTier = 1;
    a.cargoMass = 0;
    ShipSnapshot b = a;
    b.sessionTime = 1;
    b.x = 10;
    b.z = -4;
    b.heading = 0.2;
    b.sailTier = 3;
    b.cargoMass = 100;
    track.push(b);
    track.push(a); // older than the newest: dropped
    const bool dropped = track.size() == 1 && near(track.newestTime(), 1.0);
    track.snaps.clear();
    track.push(a);
    track.push(b);
    InterpShip mid;
    InterpShip held;
    InterpShip atEnd;
    const bool sampled = track.sample(0.5, mid) && track.sample(5.0, held) && track.sample(1.0, atEnd);
    const double midToA = std::fabs(wrapPi(mid.heading - 6.0));
    const double midToB = std::fabs(wrapPi(0.2 - mid.heading));
    const bool interp = dropped && sampled && near(mid.x, 5.0) && near(mid.z, -2.0) &&
                        midToA < 0.3 && midToB < 0.3 && std::fabs(mid.heading - 3.1) > 1.0 &&
                        mid.sailTier == 1 && near(mid.cargoMass, 0.0) &&
                        near(held.x, 10.0) && near(held.sessionTime, 1.0) && atEnd.sailTier == 3;
    push("Remote motion interpolates on the short heading arc and holds past the end", interp);

    const double w0 = sessionWindDir(2.1, 0.02, 0.0);
    const double w1 = sessionWindDir(2.1, 0.02, 10.0);
    const bool windOk = near(w0, 2.1) && near(sessionWindDir(2.1, 0.02, 10.0), w1) && !near(w0, w1);
    push("Session wind is a pure function of the shared clock", windOk);

    NetClock clock;
    clock.observe(10.0, 100.0);
    const double drifted = clock.now(101.0);
    clock.observe(10.90, 101.0); // 0.10 s error: keep the local clock
    const double kept = clock.now(101.0);
    clock.observe(10.40, 101.0); // 0.60 s error: snap
    const double snapped = clock.now(101.0);
    const bool clockOk = clock.synced && near(drifted, 11.0) && near(kept, 11.0) && near(snapped, 10.40);
    push("Session clock follows local time and snaps only on a large error", clockOk);

    // Two peers, in-process. Each sails a different ship; both recompute pose.
    SailSession hostPeer = SailSession::host("phase8-seed", 2.1, 0.02);
    SailSession clientPeer = SailSession::client();
    hostPeer.setHostClock(4.5);
    clientPeer.queueHello("lee");
    for (const auto& m : clientPeer.takeOutbound()) hostPeer.receive(m);
    for (const auto& m : hostPeer.takeOutbound()) clientPeer.receive(m);

    const auto hostWaves = makeWaveField(hostPeer.params.waveSeed);
    const auto clientWaves = makeWaveField(clientPeer.params.waveSeed);
    const WaterSample hs = sampleWater(hostWaves, 3.0, 4.0, 1.5);
    const WaterSample cs = sampleWater(clientWaves, 3.0, 4.0, 1.5);
    const bool seedShared = hostPeer.joined && clientPeer.joined && clientPeer.localPlayerId == 1 &&
                            clientPeer.params.waveSeed == "phase8-seed" &&
                            near(hs.height, cs.height) && near(hs.slopeX, cs.slopeX);

    snap.playerId = 0;
    hostPeer.publish(snap);
    DamageEvent hostHit{ 0, 5, 1.0, snap.sessionTime };
    hostPeer.publishDamage(hostHit);

    ShipSnapshot other = snap;
    other.playerId = 1;
    other.x = -20.0;
    other.z = 15.0;
    other.heading = -0.4;
    other.cargoMass = 0;
    other.sinkDepth = 0;
    other.pieceDamage.assign(prototype.pieces.size(), 0.0);
    const FloatPose otherPose = poseForSnapshot(prototype, clientWaves, other);
    other.heaveY = otherPose.heaveY;
    other.pitch = otherPose.pitch;
    other.heel = otherPose.heel;
    clientPeer.publish(other);

    for (const auto& m : hostPeer.takeOutbound()) clientPeer.receive(m);
    for (const auto& m : clientPeer.takeOutbound()) hostPeer.receive(m);

    Ship replica = prototype;
    applySnapshot(replica, clientPeer.latestRemote);
    if (clientPeer.hasDamage) applyDamageEvent(replica, clientPeer.lastDamage);
    const FloatPose clientView = poseForSnapshot(prototype, clientWaves, clientPeer.latestRemote);
    // The damage event is newer-or-equal and sets the same piece the snapshot
    // already carried at 0.5; the event raises it to 1.0, so pose-for-snapshot
    // (snapshot damage only) still matches the authority pose, and the event
    // then drops buoyancy further.
    const bool hostSeen = clientPeer.hasRemote && clientPeer.latestRemote.playerId == 0 &&
                          near(clientPeer.latestRemote.x, 30.0) &&
                          posesAgree(clientView, posed, 1e-4);
    const double replicaBuoy = getShipStats(replica).buoyancyScore;
    const bool damageReplicated = clientPeer.hasDamage && clientPeer.lastDamage.pieceIndex == 5 &&
                                  near(replica.pieces[5].damage, 1.0) && replicaBuoy < before;
    const bool clientSeen = hostPeer.hasRemote && hostPeer.latestRemote.playerId == 1 &&
                            near(hostPeer.latestRemote.x, -20.0) && near(hostPeer.latestRemote.z, 15.0);
    const bool twoShips = hostSeen && clientSeen &&
                          !near(clientPeer.latestRemote.x, hostPeer.latestRemote.x);
    push("Two peers join in-process and share the wave seed", seedShared);
    push("Each peer sees the other ship move", twoShips);
    push("Recomputed float pose matches the authority within tolerance", hostSeen);
    push("Host damage event replicates and lowers the client's buoyancy", damageReplicated);

    // A second hello must not reset the session or assign another player.
    const int genBefore = clientPeer.hostTimeGen;
    hostPeer.receive("SEA1 hello late");
    const bool oneClient = hostPeer.peerName == "lee" && hostPeer.joined &&
                           clientPeer.localPlayerId == 1 && clientPeer.hostTimeGen == genBefore;
    push("A second hello does not open another player", oneClient);

    UdpSocket alpha;
    UdpSocket beta;
    const bool opened = alpha.bind("127.0.0.1", 0) && beta.bind("127.0.0.1", 0) &&
                        alpha.setPeer("127.0.0.1", beta.boundPort()) &&
                        beta.setPeer("127.0.0.1", alpha.boundPort());
    bool delivered = false;
    if (opened) {
        const std::string payload = encodeSnapshot(snap);
        const bool sent = alpha.send(payload);
        std::string got;
        bool recvd = false;
        for (int i = 0; i < 100 && !recvd; ++i) recvd = beta.recv(got);
        ShipSnapshot overWire;
        delivered = sent && recvd && decodeSnapshot(got, overWire) && near(overWire.x, snap.x) &&
                    overWire.pieceDamage.size() == snap.pieceDamage.size() &&
                    near(overWire.pieceDamage[5], snap.pieceDamage[5]);
    }
    push("UDP loopback delivers a ship snapshot between two sockets", opened && delivered,
         opened ? "" : "bind failed");

    return r;
}

} // namespace sea
