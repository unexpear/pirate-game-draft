// Sea Trial — walk collision for the box-figure character.
//
// Pure math (no graphics): an upright body against axis-aligned solids. Yard
// walk and a later at-sea deck walk can both feed it scene-space or ship-local
// boxes. Public domain (The Unlicense).
#pragma once

#include "ship_model.hpp"

#include <vector>

namespace sea {

struct Aabb {
    double minX = 0, minY = 0, minZ = 0;
    double maxX = 0, maxY = 0, maxZ = 0;
};

// Upright body. Collision ignores solids that end below `feet + step` (floors,
// sills) and solids that start above `feet + height`.
struct CharacterBody {
    double radius = 0.28;
    double height = 1.72;
    double step = 0.40;
};

// Push the body out of any overlap, then apply (dx, dz). A clear step is taken
// whole. A blocked step slides: X advances to the first face it crosses, then Z
// from that X, so a long step cannot tunnel through a thin solid. Returns true
// when the requested delta was not fully applied or a push-out moved the body.
bool moveCharacter(double& x, double& z, double feetY, double dx, double dz,
                   const CharacterBody& body, const std::vector<Aabb>& solids);

// Ship solids in scene space. (originX, originY, originZ) is the hull origin
// and `yaw` is rotation about Y (no pitch or heel). Heading matches the ship:
// local +Z is forward, local +X is starboard.
//
// Off the deck, a filled length×beam envelope stands in for the hull whenever
// ribs or planks exist, so the gaps between frames are not a way inside.
// On deck that envelope is omitted (it would eject the sailor); fittings that
// rise through the body (mast, capstan, helm, bowsprit, stem, sternpost) and
// gunwale rails are the blockers. Ribs, planks, the keel, the deck, lines, and
// blocks are not individual walk solids — ribs would make the deck unwalkable.
std::vector<Aabb> shipWalkSolids(const Ship& ship, double originX, double originY,
                                 double originZ, double yaw, bool onDeck);

// Build-berth scenery matching ship_view::renderBuildScene, plus shipWalkSolids
// for the hull on the stocks (origin at x=z=0, the given originY, yaw 0).
// The gangplank steps are left open so a climb can use that corridor.
std::vector<Aabb> yardWalkSolids(const Ship& ship, double originY, bool onDeck);

std::vector<TestResult> runCharacterCollisionTests();

} // namespace sea
