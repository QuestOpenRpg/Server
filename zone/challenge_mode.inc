// Included by client.cpp. Challenge status belongs to a character, never an account.
#include "challenge_mode.h"
#include "../common/seperator.h"
bool Client::LoadChallengeMode() {
    auto r = database.QueryDatabase(fmt::format("SELECT enabled FROM takp_character_challenge WHERE character_id={}", CharacterID()));
    if (!r.Success()) return false;
    challenge_mode_enabled_ = r.RowCount() && atoi(r.begin()[0]) != 0;
    return true;
}
bool Client::SetChallengeMode(bool enabled) {
    auto r = database.QueryDatabase(fmt::format("INSERT INTO takp_character_challenge(character_id,enabled) VALUES({},{}) ON DUPLICATE KEY UPDATE enabled=VALUES(enabled)", CharacterID(), enabled ? 1 : 0));
    if (!r.Success()) return false;
    challenge_mode_enabled_ = enabled;
    EnforceChallengeBotLimit(this);
    return true;
}
unsigned Client::ChallengeBotLimit() {
    return ChallengeMode::BotLimit(IsChallengeMode(), GetLevel(), HasRaid());
}
bool Client::LeavesEquipmentCorpse() const {
    return ChallengeMode::LeaveItems(IsChallengeMode(), RuleB(Character, LeaveCorpses), GetLevel(), RuleI(Character, DeathItemLossLevel));
}
void command_challenge(Client *c, const Seperator *sep) {
    const auto mode = Strings::ToLower(sep->arg[1]);
    const auto action = Strings::ToLower(sep->arg[2]);
    if (mode != "mode") {
        c->Message(Chat::White, "Usage: #challenge mode on | #challenge mode status. Normal activation is permanent."); return;
    }
    // Intentionally unlisted, available to every account, and only affects the caller.
    if (action == "override") {
        const auto value = Strings::ToLower(sep->arg[3]);
        if (sep->argnum != 3 || (value != "on" && value != "off")) {
            c->Message(Chat::White, "Usage: #challenge mode override on|off"); return;
        }
        if (!c->SetChallengeMode(value == "on")) c->Message(Chat::Red, "Challenge Mode could not be saved. Your setting was not changed.");
        else c->Message(Chat::Yellow, "Challenge Mode override: %s. Saved for this character.", value == "on" ? "ON" : "OFF");
        return;
    }
    if (sep->argnum == 1 || (sep->argnum == 2 && action == "status")) {
        c->Message(Chat::White, "Challenge Mode: %s. Current bot capacity: %u (group/raid space still required).", c->IsChallengeMode() ? "ON" : "OFF", c->ChallengeBotLimit()); return;
    }
    if (sep->argnum != 2 || action != "on") {
        c->Message(Chat::White, "Challenge Mode cannot be turned off through the normal command. Use #challenge mode on or #challenge mode status."); return;
    }
    if (c->IsChallengeMode()) { c->Message(Chat::White, "Challenge Mode is already enabled."); return; }
    if (c->GetLevel() != 1) { c->Message(Chat::Red, "Challenge Mode can only be activated at level 1."); return; }
    if (zone->GetZoneID() == 202) { c->Message(Chat::Red, "Leave the Plane of Knowledge before enabling Challenge Mode."); return; }
    bool bound = false;
    for (const auto &bind : c->GetPP().binds) if (bind.zoneId == 202) bound = true;
    if (!ChallengeMode::CanActivate(c->GetLevel(), zone->GetZoneID(), bound)) {
        c->Message(Chat::Red, "You are bound in the Plane of Knowledge. Bind elsewhere before enabling Challenge Mode."); return;
    }
    if (!c->SetChallengeMode(true)) { c->Message(Chat::Red, "Challenge Mode could not be saved. It was not enabled."); return; }
    c->Message(Chat::Yellow, "Challenge Mode enabled permanently. PoK books unlock at 45. Bots unlock at 5, 15, 25, 35, and raid access at 45. Death XP loss and equipment corpses start now.");
}
