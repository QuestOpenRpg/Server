// Included after Owned() in player_bot.cpp's anonymous namespace.
struct OwnerFormation {
    bool loaded = false, enabled = false, gather = false, line = false, keep_pace = false;
    uint32 entity_id = 0;
    uint64 next_check = 0;
    BotFormation::Frame frame;
    BotFormation::Clearance clearance;
    std::vector<uint32_t> slots;
    std::map<uint32_t, glm::vec3> destinations;
    std::map<uint32_t, glm::vec3> steering;
};
std::map<uint32, OwnerFormation> owner_formations;

OwnerFormation& FormationFor(Client* owner) {
    auto& state = owner_formations[owner->CharacterID()];
    if (state.entity_id != owner->GetID()) { state = OwnerFormation{}; state.entity_id = owner->GetID(); }
    if (!state.loaded) {
        auto q = database.QueryDatabase(fmt::format(
            "SELECT formation_enabled,formation_gather,formation_line,keep_pace FROM takp_bot_owner_settings WHERE owner_character_id={}", owner->CharacterID()));
        state.loaded = true; // Missing migration disables safely; explicit command can report the failure.
        if(q.Success() && q.RowCount()) { state.enabled=atoi(q.begin()[0])!=0; state.gather=atoi(q.begin()[1])!=0; state.line=atoi(q.begin()[2])!=0; state.keep_pace=atoi(q.begin()[3])!=0; }
    }
    return state;
}

bool FormationDestination(PlayerBot* bot, Client* owner, BotFormation::Point offset, glm::vec3& result) {
    const auto now=Timer::GetCurrentTime();
    result = glm::vec3(bot->follow_motion_.X(now) + offset.x, bot->follow_motion_.Y(now) + offset.y, owner->GetZ());
    if (!zone->zonemap || !zone->pathing || !zone->pathing->IsUsingNavMesh()) return false;
    glm::vec3 ground = result;
    float z = zone->zonemap->FindBestZ(ground, nullptr, 20.f);
    if (z == BEST_Z_INVALID || !std::isfinite(z)) return false;
    result.z = z + bot->GetZOffset();
    if (std::abs(result.z - owner->GetZ()) > 18) return false;
    if (zone->HasWaterMap() && zone->watermap->InLiquid(result)) return false;
    if (zone->IsWaterZone(result.z)) return false;
    // Require room around the target as well as a short safe route. A projected
    // point across a wall or on a ledge must not be treated as usable floor.
    for (const auto edge : {glm::vec2(-2,0), glm::vec2(2,0), glm::vec2(0,-2), glm::vec2(0,2)}) {
        glm::vec3 sample(result.x + edge.x, result.y + edge.y, owner->GetZ());
        float ez = zone->zonemap->FindBestZ(sample, nullptr, 20.f);
        if (ez == BEST_Z_INVALID || !std::isfinite(ez) || std::abs(ez - z) > 4) return false;
        sample.z = ez + bot->GetZOffset();
        if (!zone->zonemap->CheckLoS(result, sample)) return false;
    }
    return BotFormation::SafeRoute(zone->pathing, zone->zonemap, glm::vec3(owner->GetPosition()), result) &&
           BotFormation::SafeRoute(zone->pathing, zone->zonemap, glm::vec3(bot->GetPosition()), result);
}

bool PlayerBot::FormationFollow(Client* owner) {
    auto& state = FormationFor(owner);
    if (!state.enabled) return false;
    if (!BotFormation::CanPosition(GetFollowID() == owner->GetID(), IsEngaged(), IsCasting(), (options_["suspend"] != 0 && !Retreating()), IsRooted()))
        return true; // Never let fallback movement override a casting/suspended bot.
    const auto now = Timer::GetCurrentTime();
    if (now >= state.next_check) {
        state.next_check = now + 200;
        auto bots = Owned(owner);
        std::vector<uint32_t> present;
        for (auto* b : bots) if (b->GetFollowID() == owner->GetID() && (!b->options_["suspend"] || b->Retreating())) present.push_back(b->ID());
        auto previous_slots = state.slots;
        BotFormation::Reconcile(state.slots, present);
        if (previous_slots != state.slots) state.clearance = BotFormation::Clearance{};
        state.frame.Update(follow_motion_.X(now), follow_motion_.Y(now), owner->GetHeading());
        state.destinations.clear();
        state.steering.clear();
        bool clear = !state.slots.empty();
        std::vector<float> sizes;
        for(auto id:state.slots) { auto it=std::find_if(bots.begin(),bots.end(),[&](PlayerBot* b){return b->ID()==id;}); sizes.push_back(it==bots.end()?6.f:(*it)->GetSize()); }
        const auto layout=BotFormation::Layout(sizes,owner->GetSize(),state.gather,state.line);
        for (unsigned i = 0; i < state.slots.size(); ++i) {
            auto it = std::find_if(bots.begin(), bots.end(), [&](PlayerBot* b) { return b->ID() == state.slots[i]; });
            if (it == bots.end()) { clear = false; break; }
            auto* b = *it;
            // Combat/casting retains a slot, but does not steer the active bot.
            glm::vec3 destination;
            auto offset = BotFormation::Rotate(layout[i], state.frame.angle);
            if (!FormationDestination(b, owner, offset, destination)) { clear = false; break; }
                        state.destinations[b->ID()] = destination;
            glm::vec3 steering=destination;
            if(follow_motion_.Moving(now)) {
                auto lead=offset;
                lead.x+=follow_motion_.dir_x*follow_motion_.speed*BotFollow::SteeringSeconds;
                lead.y+=follow_motion_.dir_y*follow_motion_.speed*BotFollow::SteeringSeconds;
                glm::vec3 candidate;
                if(FormationDestination(b,owner,lead,candidate)) steering=candidate;
            }
            state.steering[b->ID()]=steering;
        }
        state.clearance.Update(clear);
    }
    if (!state.clearance.active) {
        if (formation_moving_) StopNavigation();
        formation_moving_ = false;
        return false; // Existing following and its existing pathfinder handle narrow areas.
    }
    auto found = state.destinations.find(ID());
    if (found == state.destinations.end()) return false;
    const auto& destination = found->second;
    const auto steering_it=state.steering.find(ID());
    const auto& steering=steering_it==state.steering.end()?destination:steering_it->second;
    float distance = glm::length2(glm::vec2(GetX()-steering.x, GetY()-steering.y));
    if (distance <= (formation_moving_ ? .5625f : 1.f)) {
        if(formation_moving_ || IsMoving()) StopNavigation();
        formation_moving_ = false;
        // Correct the final approach once, using the heading captured when
        // the owner stopped. Looking around later must not turn idle bots.
        if(!follow_motion_.Moving(now) && arrival_facing_pending_) {
            arrival_facing_pending_=false;
            if(GetAppearance()!=eaSitting && GetHeading()!=arrival_facing_) {
                SetHeading(arrival_facing_);
                SendRealPosition();
            }
        }
    } else {
        if (GetAppearance() == eaSitting) SetAppearance(eaStanding);
        RunTo(steering.x, steering.y, steering.z);
        formation_moving_ = true;
    }
    return true;
}

bool FormationCommand(Client* owner, const std::string& action, const char* argument, const char* toggle) {
    if (action != "formation") return false;
    auto& state = FormationFor(owner);
    const auto value = Strings::ToLower(argument);
    if (value.empty() || value == "status") {
        owner->Message(Chat::White,"Formation %s; gather %s; line %s. #bot formation on|off; #bot formation gather on|off; #bot formation line on|off.",state.enabled?"ON":"OFF",state.gather?"ON":"OFF",state.line?"ON":"OFF");
        return true;
    }
    bool enabled=state.enabled, gather=state.gather, line=state.line;
    const auto option=Strings::ToLower(toggle);
    if ((value=="gather" || value=="line") && (option=="on" || option=="off")) {
        const bool on=option=="on";
        if(value=="gather") gather=on; else line=on;
        if(on) enabled=true;
    } else if ((value=="on" || value=="off") && option.empty()) enabled=value=="on";
    else { owner->Message(Chat::White,"Use #bot formation on|off|status, #bot formation gather on|off, or #bot formation line on|off."); return true; }
    auto q=database.QueryDatabase(fmt::format("INSERT INTO takp_bot_owner_settings(owner_character_id,formation_enabled,formation_gather,formation_line) VALUES({},{},{},{}) ON DUPLICATE KEY UPDATE formation_enabled=VALUES(formation_enabled),formation_gather=VALUES(formation_gather),formation_line=VALUES(formation_line)",owner->CharacterID(),enabled,gather,line));
    if(!q.Success()) {owner->Message(Chat::White,"Formation setting could not be saved. Check migration 010.");return true;}
    state.enabled=enabled;state.gather=gather;state.line=line;
    state.clearance = BotFormation::Clearance{};
    state.frame = BotFormation::Frame{};
    state.next_check = 0;
    for (auto* b : Owned(owner)) b->ResetFormationMotion();
    owner->Message(Chat::White,"Formation %s; gather %s; line %s. Saved for this character.", enabled ? "ON" : "OFF", gather?"ON":"OFF",line?"ON":"OFF");
    return true;
}

bool PlayerBot::AtFormationRest() {
 auto* owner = Owner();
 if (!owner || GetFollowID()!=owner->GetID()) return false;
 auto& state = FormationFor(owner);
 if (!state.enabled || !state.clearance.active) return false;
 auto it=state.destinations.find(ID());
 return it!=state.destinations.end() && glm::length2(glm::vec2(GetX()-it->second.x,GetY()-it->second.y))<=1.f;
}
bool PlayerBot::NormalFollow(Client* owner) {
 if(!BotFormation::CanPosition(GetFollowID()==owner->GetID(),IsEngaged(),IsCasting(),(options_["suspend"]!=0 && !Retreating()),IsRooted())) {follow_moving_=false;return true;}
 const bool moving=follow_motion_.Moving(Timer::GetCurrentTime());
 if(moving) arrival_facing_pending_=false;
 const float gap=glm::distance(glm::vec2(GetPosition()),glm::vec2(follow_motion_.X(Timer::GetCurrentTime()),follow_motion_.Y(Timer::GetCurrentTime())));
 const float desired=std::max(3.f,std::sqrt(static_cast<float>(GetFollowDistance())));
 if(!BotFollow::ShouldMove(gap,desired,moving,follow_moving_)) {
  if(follow_moving_ || IsMoving()) StopNavigation();
  follow_moving_=false;follow_pace_filter_.Reset();return true;
 }
 follow_moving_=true;
 if(GetAppearance()==eaSitting) SetAppearance(eaStanding);
 const auto target=BotFollow::FollowTarget(GetX(),GetY(),follow_motion_.X(Timer::GetCurrentTime()),follow_motion_.Y(Timer::GetCurrentTime()),desired,moving,follow_motion_.speed);
 glm::vec3 destination(target.x,target.y,owner->GetZ());
 // Never project a follow target through a wall or over missing ground.
 if(!BotFormation::SafeRoute(zone->pathing,zone->zonemap,glm::vec3(owner->GetPosition()),destination)) {
  const auto fallback=BotFollow::FollowTarget(GetX(),GetY(),owner->GetX(),owner->GetY(),desired,false);
  destination=glm::vec3(fallback.x,fallback.y,owner->GetZ());
  if(!BotFormation::SafeRoute(zone->pathing,zone->zonemap,glm::vec3(owner->GetPosition()),destination)) destination=glm::vec3(owner->GetPosition());
 }
 destination.z=GetFixedZ(destination);
 RunTo(destination.x,destination.y,destination.z);
 return true;
}

int PlayerBot::FollowPace(int maximum) {
 auto* owner=Owner();
 if(!owner || GetFollowID()!=owner->GetID() || IsEngaged() || IsCasting() || IsFeared() || (options_["suspend"] && !Retreating()) || (!follow_moving_&&!formation_moving_)) return maximum;
 const auto now=Timer::GetCurrentTime();
 const bool moving=follow_motion_.Moving(now);
 float error=0;
 auto& state=FormationFor(owner);
 auto target=state.destinations.find(ID());
 if(formation_moving_ && state.enabled && state.clearance.active && target!=state.destinations.end()) {
    const auto difference=glm::vec2(target->second)-glm::vec2(GetPosition());
  // Signed along-track error: do not speed up a bot already ahead of its slot.
  error=moving?glm::dot(difference,glm::vec2(follow_motion_.dir_x,follow_motion_.dir_y)):glm::length(difference);
 } else error=glm::distance(glm::vec2(GetPosition()),glm::vec2(follow_motion_.X(Timer::GetCurrentTime()),follow_motion_.Y(Timer::GetCurrentTime())))-std::max(3.f,std::sqrt(static_cast<float>(GetFollowDistance())));
  const bool keep=state.keep_pace && GetSnaredAmount()<=0 && !IsRooted() && !IsStunned() && !IsMezzed() && !owner->GetAggroCount();
  const int target_pace=BotFollow::Pace(maximum,follow_motion_.speed,error,moving,keep);
 if(!moving || GetSnaredAmount()>0 || maximum<=0) {follow_pace_filter_.Reset();return target_pace;}
 const int smoothed=follow_pace_filter_.Update(now,target_pace);
 return keep?smoothed:std::min(maximum,smoothed);
}
bool KeepPaceCommand(Client* owner,const std::string& action,const char* argument,const char* extra) {
 if(action!="keeppace") return false;
 auto& state=FormationFor(owner);
 const auto value=Strings::ToLower(argument);
 if((value.empty() || value=="status") && !*extra) {
  owner->Message(Chat::White,"Keep pace: %s. Use #bot keeppace on or off. Saved per character; applies to your following bots.",state.keep_pace?"ON":"OFF");return true;
 }
 if((value!="on" && value!="off") || *extra) {owner->Message(Chat::White,"Use #bot keeppace on | off | status.");return true;}
 const bool enabled=value=="on";
 auto q=database.QueryDatabase(fmt::format("INSERT INTO takp_bot_owner_settings(owner_character_id,keep_pace) VALUES({},{}) ON DUPLICATE KEY UPDATE keep_pace=VALUES(keep_pace)",owner->CharacterID(),enabled));
 if(!q.Success()){owner->Message(Chat::White,"Keep pace could not be saved. Check migration 011.");return true;}
 state.keep_pace=enabled;
 state.next_check=0;
 for(auto* b:Owned(owner)) b->ResetFormationMotion();
 owner->Message(Chat::White,"Keep pace %s, saved for this character. Following bots %s.",enabled?"ON":"OFF",enabled?"match your movement speed and catch up outside combat":"use their normal speed limit");
 return true;
}

void PlayerBot::CheckFollowStop(Client* owner) {
 const bool moving=follow_motion_.Moving(Timer::GetCurrentTime());
 if(owner_was_moving_ && !moving) {
  // Discard extrapolated routes as soon as the owner's stop is received,
  // including while the idle AI timer is waiting for its next update.
  ResetFormationMotion();
  auto& state=FormationFor(owner);
  arrival_facing_pending_=state.enabled && GetFollowID()==owner->GetID() && !forced_sit_;
  arrival_facing_=owner->GetHeading();
  state.next_check=0;
  state.destinations.clear();
  state.steering.clear();
 }
 owner_was_moving_=moving;
}
