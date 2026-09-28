// Sea Trial — walk collision for the box-figure character. Public domain (Unlicense).
#include "character_collision.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sea {

namespace {

bool overlapsBody(double feetY, const CharacterBody& body, const Aabb& box) {
    const double lo = feetY + body.step;
    const double hi = feetY + body.height;
    return box.maxY > lo && box.minY < hi;
}

// Least-penetration push onto the expanded face. Standing on the face is free
// (the inside test is strict).
bool pushOut(double& x, double& z, double feetY, const CharacterBody& body,
             const std::vector<Aabb>& solids) {
    bool moved = false;
    for (int pass = 0; pass < 8; ++pass) {
        bool any = false;
        for (const Aabb& box : solids) {
            if (!overlapsBody(feetY, body, box)) continue;
            const double minX = box.minX - body.radius;
            const double maxX = box.maxX + body.radius;
            const double minZ = box.minZ - body.radius;
            const double maxZ = box.maxZ + body.radius;
            if (!(x > minX && x < maxX && z > minZ && z < maxZ)) continue;
            const double penL = x - minX;
            const double penR = maxX - x;
            const double penB = z - minZ;
            const double penF = maxZ - z;
            const double m = std::min(std::min(penL, penR), std::min(penB, penF));
            if (m == penL) x = minX;
            else if (m == penR) x = maxX;
            else if (m == penB) z = minZ;
            else z = maxZ;
            any = true;
            moved = true;
        }
        if (!any) break;
    }
    return moved;
}

Aabb yawedBox(double ox, double oy, double oz, double yaw,
              double lx, double ly, double lz, double sx, double sy, double sz) {
    const double c = std::cos(yaw);
    const double s = std::sin(yaw);
    const double wx = ox + lx * c + lz * s;
    const double wy = oy + ly;
    const double wz = oz - lx * s + lz * c;
    const double hx = std::fabs(c) * sx * 0.5 + std::fabs(s) * sz * 0.5;
    const double hy = sy * 0.5;
    const double hz = std::fabs(s) * sx * 0.5 + std::fabs(c) * sz * 0.5;
    return { wx - hx, wy - hy, wz - hz, wx + hx, wy + hy, wz + hz };
}

void addPiece(std::vector<Aabb>& out, const Piece& p,
              double ox, double oy, double oz, double yaw) {
    out.push_back(yawedBox(ox, oy, oz, yaw,
                           p.position.x, p.position.y, p.position.z,
                           p.bounds.x, p.bounds.y, p.bounds.z));
}

void addBox(std::vector<Aabb>& out, double x, double y, double z,
            double sx, double sy, double sz) {
    out.push_back({ x - sx * 0.5, y - sy * 0.5, z - sz * 0.5,
                    x + sx * 0.5, y + sy * 0.5, z + sz * 0.5 });
}

bool hasType(const Ship& ship, const char* type) {
    for (const Piece& p : ship.pieces)
        if (p.type == type) return true;
    return false;
}

const Piece* findType(const Ship& ship, const char* type) {
    for (const Piece& p : ship.pieces)
        if (p.type == type) return &p;
    return nullptr;
}

} // namespace

bool moveCharacter(double& x, double& z, double feetY, double dx, double dz,
                   const CharacterBody& body, const std::vector<Aabb>& solids) {
    const bool pushed = pushOut(x, z, feetY, body, solids);
    if (dx == 0.0 && dz == 0.0) return pushed;

    // Axis slide against the expanded faces. The clamp is the first face the
    // segment actually crosses, so a long step cannot tunnel through a thin wall
    // whose far side is clear.
    auto sweep = [&](double pos, double delta, double other, bool alongX) {
        if (delta == 0.0) return pos;
        const double dest = pos + delta;
        double best = dest;
        const double r = body.radius;
        for (const Aabb& box : solids) {
            if (!overlapsBody(feetY, body, box)) continue;
            const double crossMin = (alongX ? box.minZ : box.minX) - r;
            const double crossMax = (alongX ? box.maxZ : box.maxX) + r;
            if (!(other > crossMin && other < crossMax)) continue;
            const double enter = (alongX ? box.minX : box.minZ) - r;
            const double exit = (alongX ? box.maxX : box.maxZ) + r;
            if (pos > enter && pos < exit) return pos;
            if (delta > 0.0) {
                if (pos <= enter && dest > enter) best = std::min(best, enter);
            } else if (pos >= exit && dest < exit) {
                best = std::max(best, exit);
            }
        }
        return best;
    };

    const double x1 = sweep(x, dx, z, true);
    const double z1 = sweep(z, dz, x1, false);
    const bool clipped = (x1 != x + dx) || (z1 != z + dz);
    x = x1;
    z = z1;
    return pushed || clipped;
}

std::vector<Aabb> shipWalkSolids(const Ship& ship, double originX, double originY,
                                 double originZ, double yaw, bool onDeck) {
    std::vector<Aabb> out;
    const double length = ship.bounds.length;
    const double width = ship.bounds.width;
    const double depth = ship.bounds.depth;

    // Filled footprint of the hull on the stocks. Local Y runs from the keel
    // bottom up through the topsides, which puts the box through a dock walker
    // (feet near the block tops) without covering the deck the sailor stands on.
    if (!onDeck && (hasType(ship, "rib") || hasType(ship, "plank"))) {
        const double localMinY = -depth * 0.55 - 0.09;
        const double localMaxY = depth * 0.50;
        const double cy = (localMinY + localMaxY) * 0.5;
        const double sy = localMaxY - localMinY;
        out.push_back(yawedBox(originX, originY, originZ, yaw,
                               0.0, cy, 0.0, width, sy, length));
    }

    for (const Piece& p : ship.pieces) {
        if (p.type == "mast" || p.type == "capstan" || p.type == "helm"
            || p.type == "bowsprit" || p.type == "stem" || p.type == "sternpost")
            addPiece(out, p, originX, originY, originZ, yaw);
    }

    if (onDeck && width > 0.0 && length > 0.0) {
        double deckTop = depth * 0.03 + 0.04;
        if (const Piece* deck = findType(ship, "deck"))
            deckTop = deck->position.y + deck->bounds.y * 0.5;
        const double railH = 1.05;
        const double railT = 0.20;
        const double railY = deckTop + railH * 0.5;
        const double sideX = width * 0.46;
        const double zHalf = length * 0.5 - 0.20;
        // Port and starboard gunwales.
        out.push_back(yawedBox(originX, originY, originZ, yaw,
                               -sideX, railY, 0.0, railT, railH, zHalf * 2.0));
        out.push_back(yawedBox(originX, originY, originZ, yaw,
                                sideX, railY, 0.0, railT, railH, zHalf * 2.0));
        // Bow rail, and a stern rail split so the gangplank mouth stays open.
        const double endZ = length * 0.5 - 0.15;
        out.push_back(yawedBox(originX, originY, originZ, yaw,
                               0.0, railY, endZ, sideX * 2.0, railH, railT));
        const double gap = 1.05;
        const double outer = sideX;
        if (outer > gap + 0.05) {
            const double seg = outer - gap;
            const double cx = gap + seg * 0.5;
            out.push_back(yawedBox(originX, originY, originZ, yaw,
                                   -cx, railY, -endZ, seg, railH, railT));
            out.push_back(yawedBox(originX, originY, originZ, yaw,
                                    cx, railY, -endZ, seg, railH, railT));
        }
    }
    return out;
}

std::vector<Aabb> yardWalkSolids(const Ship& ship, double originY, bool onDeck) {
    // Centres and sizes match ship_view::renderBuildScene. The dock slab, the
    // distant island, and the gangplank steps are floors or a climb corridor.
    std::vector<Aabb> out = shipWalkSolids(ship, 0.0, originY, 0.0, 0.0, onDeck);

    addBox(out, 0.0, 10.0, 40.0, 48.0, 20.0, 22.0);   // ship hall
    addBox(out, 0.0, 20.6, 40.0, 50.0, 1.6, 24.0);    // hall roof (above a walker)
    for (int s = -1; s <= 1; s += 2) {
        addBox(out, s * 23.0, 10.0, -2.0, 1.8, 20.0, 1.8); // gantry posts
        addBox(out, s * 23.0, 19.0, 4.0, 1.8, 1.8, 22.0);  // crane beams
    }
    addBox(out,  28.0, 2.0, 8.0, 4.0, 3.0, 16.0);     // timber stacks
    addBox(out, -28.0, 2.0, 8.0, 4.0, 3.0, 16.0);
    addBox(out, -23.0, 6.2, 6.0, 0.5, 0.7, 0.5);      // hoist block
    addBox(out, -23.0, 5.2, 6.0, 0.45, 0.45, 7.0);    // suspended spar

    const double len = ship.bounds.length;
    const double wid = ship.bounds.width;
    const int nb = 6;
    for (int i = 0; i < nb; ++i) {
        const double z = -len * 0.42 + (len * 0.84) * i / double(nb - 1);
        addBox(out, 0.0, 1.9, z, 1.7, 2.0, 1.1);      // keel blocks
    }
    const int np = 5;
    for (int i = 0; i < np; ++i) {
        const double z = -len * 0.34 + (len * 0.68) * i / double(np - 1);
        addBox(out, -wid * 0.66, 2.7, z, 0.45, 4.6, 0.45); // shoring posts
        addBox(out,  wid * 0.66, 2.7, z, 0.45, 4.6, 0.45);
    }
    addBox(out, -wid * 0.66, 4.9, 0.0, 0.28, 0.28, len * 0.7); // scaffold rails
    addBox(out,  wid * 0.66, 4.9, 0.0, 0.28, 0.28, len * 0.7);
    return out;
}

std::vector<TestResult> runCharacterCollisionTests() {
    std::vector<TestResult> r;
    auto push = [&](const std::string& name, bool pass, const std::string& details = "") {
        r.push_back({ name, pass, details });
    };
    auto near = [](double a, double b, double tol = 0.02) { return std::fabs(a - b) < tol; };

    const CharacterBody body;
    const double feet = 0.0;
    // A wall across +Z, tall enough to meet the body above the step.
    const std::vector<Aabb> wall = { { -2.0, 0.0, 1.0, 2.0, 3.0, 1.4 } };

    {
        double x = 0.0, z = 0.0;
        moveCharacter(x, z, feet, 0.5, -0.25, body, wall);
        push("Open ground takes the full step", near(x, 0.5) && near(z, -0.25),
             "ended at " + std::to_string(x) + ", " + std::to_string(z));
    }
    {
        double x = 0.0, z = 0.0;
        const bool blocked = moveCharacter(x, z, feet, 0.0, 3.0, body, wall);
        const double stop = 1.0 - body.radius; // flush with the near face
        push("A wall blocks a step forward",
             blocked && near(x, 0.0) && near(z, stop) && z < 1.0,
             "ended at z " + std::to_string(z));
    }
    {
        double x = 0.0, z = 0.0;
        moveCharacter(x, z, feet, 0.8, 3.0, body, wall);
        const double stop = 1.0 - body.radius;
        push("A blocked step slides along the wall",
             near(x, 0.8) && near(z, stop) && z < 1.0,
             "ended at " + std::to_string(x) + ", " + std::to_string(z));
    }
    {
        // Top is below feet+step, so the body walks over it.
        const std::vector<Aabb> sill = { { -1.0, 0.0, 0.2, 1.0, 0.25, 0.6 } };
        double x = 0.0, z = 0.0;
        const bool blocked = moveCharacter(x, z, feet, 0.0, 1.0, body, sill);
        push("A sill below the step is not a wall", !blocked && near(z, 1.0),
             "ended at z " + std::to_string(z));
    }

    ShipConfig cfg;
    cfg.name = "Collision Sloop";
    const Ship ship = makeShipFromConfig(cfg);
    const double originY = 2.9 + ship.bounds.depth * 0.55;
    const double dockFeet = 1.8;
    const double deckFeet = originY + ship.bounds.depth * 0.03 + 0.06;

    {
        auto solids = shipWalkSolids(ship, 0.0, originY, 0.0, 0.0, false);
        double x = 0.0, z = 0.0;
        moveCharacter(x, z, dockFeet, 0.0, 0.0, body, solids);
        const double outX = ship.bounds.width * 0.5 + body.radius;
        push("The hull on the stocks pushes a dock walker out",
             std::fabs(x) + 1e-6 >= outX - 0.02 && std::fabs(z) < 0.05,
             "ended at " + std::to_string(x) + ", " + std::to_string(z));
    }
    {
        const double yaw = 1.5707963267948966; // beam now lies across Z
        auto solids = shipWalkSolids(ship, 0.0, originY, 0.0, yaw, false);
        double x = 0.0, z = 0.0;
        moveCharacter(x, z, dockFeet, 0.0, 0.0, body, solids);
        const double outZ = ship.bounds.width * 0.5 + body.radius;
        push("A yawed hull blocks across its beam",
             std::fabs(z) + 1e-6 >= outZ - 0.02 && std::fabs(x) < 0.05,
             "ended at " + std::to_string(x) + ", " + std::to_string(z));
    }
    {
        auto solids = shipWalkSolids(ship, 0.0, originY, 0.0, 0.0, true);
        double x = 0.0, z = 2.0;
        const bool blocked = moveCharacter(x, z, deckFeet, 0.0, 0.4, body, solids);
        push("On deck, a clear step ahead of the mast is free",
             !blocked && near(x, 0.0) && near(z, 2.4),
             "ended at " + std::to_string(x) + ", " + std::to_string(z));

        // Mast centre is -length*0.05, half-depth 0.16. Approach from forward.
        x = 0.0;
        z = 0.5;
        moveCharacter(x, z, deckFeet, 0.0, -2.0, body, solids);
        const double mastZ = -ship.bounds.length * 0.05;
        const double face = mastZ + 0.16 + body.radius;
        push("On deck, the mast stops a step into it",
             near(x, 0.0) && near(z, face) && z > mastZ,
             "ended at z " + std::to_string(z) + " face " + std::to_string(face));
    }
    {
        auto solids = yardWalkSolids(ship, originY, false);
        double x = 0.0, z = 20.0;
        moveCharacter(x, z, dockFeet, 0.0, 20.0, body, solids);
        const double face = 29.0 - body.radius; // hall near face, centre z=40 size 22
        push("The ship hall stops a walk up the yard",
             near(x, 0.0) && near(z, face) && z < 29.0,
             "ended at z " + std::to_string(z));

        // Shoring post outer face: centre at width*0.66, half-size 0.225.
        x = 4.0;
        z = 0.0;
        moveCharacter(x, z, dockFeet, -2.0, 0.0, body, solids);
        const double postOuter = ship.bounds.width * 0.66 + 0.225 + body.radius;
        push("A shoring post blocks a walk into the berth",
             near(z, 0.0) && near(x, postOuter) && x > ship.bounds.width * 0.66,
             "ended at x " + std::to_string(x));

        // Gangplank steps run through z=-8 on the reference sloop. They are not solids.
        x = 0.0;
        z = -8.0;
        const bool blocked = moveCharacter(x, z, dockFeet, 0.0, 0.0, body, solids);
        push("The gangplank corridor is not a solid wall",
             !blocked && near(x, 0.0) && near(z, -8.0),
             "ended at " + std::to_string(x) + ", " + std::to_string(z));
    }
    {
        Ship frames = ship;
        frames.pieces.erase(
            std::remove_if(frames.pieces.begin(), frames.pieces.end(),
                           [](const Piece& p) {
                               return p.type != "keel" && p.type != "stem"
                                   && p.type != "sternpost" && p.type != "rib";
                           }),
            frames.pieces.end());
        auto solids = shipWalkSolids(frames, 0.0, originY, 0.0, 0.0, false);
        double x = 0.0, z = 0.0;
        moveCharacter(x, z, dockFeet, 0.0, 0.0, body, solids);
        const double outX = frames.bounds.width * 0.5 + body.radius;
        push("Frames alone still fill the hull on the stocks",
             std::fabs(x) + 1e-6 >= outX - 0.02,
             "ended at x " + std::to_string(x));
    }
    return r;
}

} // namespace sea
