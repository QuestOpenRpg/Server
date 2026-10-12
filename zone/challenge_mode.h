#pragma once
#include <cstdint>
namespace ChallengeMode {
// The raid/group capacity still applies after the level restriction is lifted.
inline unsigned BotLimit(bool enabled, unsigned level, bool in_raid) {
    if (!enabled) return 5;
    if (level >= 45) return in_raid ? 71 : 5;
    if (level < 5) return 0;
    return 1 + (level - 5) / 10;
}
inline bool CanActivate(unsigned level, unsigned zone, bool bound_in_pok) {
    return level == 1 && zone != 202 && !bound_in_pok;
}
inline bool BlockBook(bool enabled, unsigned level, bool destination_pok) {
    return enabled && level < 45 && destination_pok;
}
inline bool LeaveItems(bool enabled, bool leave_corpses, unsigned level, unsigned loss_level) {
    return enabled || (leave_corpses && level >= loss_level);
}
} // namespace ChallengeMode
