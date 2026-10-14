#pragma once
#include "player_bot_formation.h"
#include "pathfinder_interface.h"
namespace BotFormation {
// Legacy waypoint graphs are too sparse to validate individual formation
// slots. Validate a short, supported corridor against collision geometry
// instead; never infer traversable floor from line of sight alone.
template<class Ground, class Visible>
inline bool SupportedCorridor(const glm::vec3& from, const glm::vec3& to, Ground ground, Visible visible) {
    const float distance = glm::length(to - from);
    if (!std::isfinite(distance) || distance > 128.f || !visible(from, to)) return false;
    const unsigned steps = std::max(1u, static_cast<unsigned>(std::ceil(distance / 2.f)));
    float previous = 0;
    glm::vec3 previous_floor;
    for (unsigned i = 0; i <= steps; ++i) {
        const auto point = from + (to - from) * (static_cast<float>(i) / steps);
        float height;
        if (!ground(point, height) || !std::isfinite(height) ||
            height > point.z + 1.f || point.z - height > 18.f ||
            (i && std::abs(height - previous) > 4.f)) return false;
        glm::vec3 floor(point.x, point.y, height + 1.f);
        if (i && !visible(previous_floor, floor)) return false;
        for (auto edge : {glm::vec2(-2,0), glm::vec2(2,0), glm::vec2(0,-2), glm::vec2(0,2)}) {
            glm::vec3 sample(point.x + edge.x, point.y + edge.y, point.z);
            float side;
            if (!ground(sample, side) || !std::isfinite(side) || std::abs(side-height) > 4.f ||
                !visible(floor, glm::vec3(sample.x, sample.y, side + 1.f))) return false;
        }
        previous = height; previous_floor = floor;
    }
    return true;
}
inline bool SafeRoute(IPathfinder* pathing, Map* zonemap, const glm::vec3& from, const glm::vec3& to) {
    if (!pathing || !zonemap) return false;
    if (!pathing->IsUsingNavMesh()) {
        return SupportedCorridor(from, to,
            [zonemap](const glm::vec3& point, float& height) {
                glm::vec3 hit;
                if (!zonemap->LineIntersectsZone(point + glm::vec3(0,0,1), point - glm::vec3(0,0,18), 1.f, &hit)) return false;
                height = hit.z; return true;
            },
            [zonemap](const glm::vec3& a, const glm::vec3& b) { return zonemap->CheckLoS(a,b); });
    }
    if (!zonemap->CheckLoS(from, to)) return false;
    bool partial = false, stuck = false;
    auto route = pathing->FindRoute(from, to, partial, stuck,
        PathingNotDisabled & ~(PathingZoneLine | PathingPortal | PathingLava | PathingSlime));
    if (partial || stuck || route.empty()) return false;
    float length = 0;
    auto previous = from;
    for (const auto& node : route) {
        if (node.teleport) return false;
        // Mesh points are ground positions; compare vertical differences between nodes,
        // allowing the normal model-height offset from the start/end positions.
        if (std::abs(node.pos.z - previous.z) > 20) return false;
        length += glm::length(node.pos - previous);
        previous = node.pos;
    }
    if (glm::length(glm::vec2(previous.x - to.x, previous.y - to.y)) > 3 || std::abs(previous.z - to.z) > 15) return false;
    return length <= glm::length(to - from) * 1.5f + 12;
}


}
