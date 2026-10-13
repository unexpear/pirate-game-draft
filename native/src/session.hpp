// Sea Trial — Phase 8 two-player sailing session. Public domain (The Unlicense).
//
// Minimal, deterministic-friendly sync for the vertical slice's optional
// two-player mini-test. No Steamworks and no host migration.
//
// The host owns the sea: wave seed, wind, and session clock. Each player owns
// their own ship and publishes a snapshot (position, heading, sail, cargo,
// piece damage, sink). The peer applies that snapshot and recomputes the float
// pose from the shared Gerstner field — the water surface is never replicated.
// A damage event carries the same hull change so a missed snapshot still
// converges, and applying it twice is a no-op.
//
// Transport is a byte string. Tests pass those strings in-process. The app
// sends them as UDP datagrams on loopback or a LAN (see UdpSocket).
#pragma once

#include "ship_model.hpp"

#include <string>
#include <vector>

namespace sea {

constexpr int kSessionProtocol = 1;

// Absolute runtime state of one player's ship at one session time.
struct ShipSnapshot {
    int playerId = 0;
    double sessionTime = 0;
    double windDir = 0;
    double x = 0, z = 0;
    double heading = 0;
    double speed = 0;
    int sailTier = 1;
    double cargoMass = 0;
    double sinkDepth = 0;
    // Authority's displayed pose (sink already applied), for the peer overlay.
    double heaveY = 0, pitch = 0, heel = 0;
    std::vector<double> pieceDamage;
};

// Absolute damage write. pieceIndex < 0 repairs every piece.
struct DamageEvent {
    int playerId = 0;
    int pieceIndex = -1;
    double damage = 0;
    double sessionTime = 0;
};

struct SessionParams {
    int protocol = kSessionProtocol;
    std::string waveSeed = "sea-trial-native";
    double windBase = 2.1;
    double windRate = 0.02; // radians per second, added to windBase
};

// Kinematic sample used to draw a remote hull between snapshots.
struct InterpShip {
    double x = 0, z = 0;
    double heading = 0;
    double speed = 0;
    double heaveY = 0, pitch = 0, heel = 0;
    double sinkDepth = 0;
    int sailTier = 1;
    double cargoMass = 0;
    double sessionTime = 0;
};

// Wind is a pure function of the session clock so both peers agree without
// sharing per-frame deltas.
double sessionWindDir(double windBase, double windRate, double sessionTime);

std::string encodeSnapshot(const ShipSnapshot& snap);
bool decodeSnapshot(const std::string& text, ShipSnapshot& snap);
std::string encodeDamage(const DamageEvent& ev);
bool decodeDamage(const std::string& text, DamageEvent& ev);
std::string encodeWelcome(const SessionParams& params, int assignedId, double sessionTime);
bool decodeWelcome(const std::string& text, SessionParams& params, int& assignedId, double& sessionTime);
std::string encodeHello(const std::string& name);
bool decodeHello(const std::string& text, std::string& name);

// Copy cargo and piece damage onto `ship`. Damage is clamped to 0..1.
// Extra local pieces keep their previous damage when the snapshot is shorter.
void applySnapshot(Ship& ship, const ShipSnapshot& snap);
void fillSnapshotDamage(ShipSnapshot& snap, const Ship& ship);
// Idempotent. Returns false when pieceIndex is past the end of `ship`.
bool applyDamageEvent(Ship& ship, const DamageEvent& ev);

// Fresh prototype + snapshot + shared waves → the pose the peer should show.
FloatPose poseForSnapshot(const Ship& prototype, const std::vector<Wave>& waves,
                          const ShipSnapshot& snap);
bool posesAgree(const FloatPose& a, const FloatPose& b, double tolerance);

// Host clock as seen by a client. `observe` snaps on the first sample and
// whenever the error exceeds 0.25 s; smaller errors let the local clock run
// so one-way delay does not rewind the waves every packet.
struct NetClock {
    bool synced = false;
    double offset = 0;
    void observe(double hostTime, double localTime);
    double now(double localTime) const;
};

// Bounded history of a remote ship, oldest first. Discrete fields (sail tier,
// cargo) hold the older key until the sample time reaches the newer key.
// Past the newest snapshot the sample holds; it does not extrapolate.
struct RemoteTrack {
    void push(const ShipSnapshot& snap);
    bool sample(double sessionTime, InterpShip& out) const;
    double newestTime() const;
    int size() const { return static_cast<int>(snaps.size()); }
    std::vector<ShipSnapshot> snaps;
};

enum class SessionRole { Host, Client };

// One peer. Outbound datagrams queue until takeOutbound(); receive() applies
// inbound datagrams. The host marks itself joined when a hello arrives and
// answers with a welcome. A second hello is ignored.
struct SailSession {
    SessionRole role = SessionRole::Host;
    int localPlayerId = 0;
    bool joined = false;
    SessionParams params;
    std::string peerName;
    RemoteTrack remote;
    ShipSnapshot latestRemote;
    bool hasRemote = false;
    DamageEvent lastDamage;
    bool hasDamage = false;
    int hostTimeGen = 0;
    double hostTime = 0;

    static SailSession host(const std::string& waveSeed, double windBase = 2.1, double windRate = 0.02);
    static SailSession client();

    void setHostClock(double sessionTime);
    void queueHello(const std::string& name);
    void publish(const ShipSnapshot& snap);
    void publishDamage(const DamageEvent& ev);
    void receive(const std::string& datagram);
    std::vector<std::string> takeOutbound();

    double hostNow = 0;
    std::vector<std::string> outbox;
};

// Non-blocking IPv4 UDP. Hosts bind and adopt the first peer that sends.
// Clients bind an ephemeral port and setPeer() before sending. Copying is
// disabled; the destructor closes the socket.
struct UdpSocket {
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool bind(const std::string& host, int port);
    bool setPeer(const std::string& host, int port);
    bool send(const std::string& payload);
    bool recv(std::string& payload);
    void close();
    int boundPort() const;
    bool isOpen() const;

private:
    struct Impl;
    Impl* impl = nullptr;
};

std::vector<TestResult> runSessionSelfTest();

} // namespace sea
