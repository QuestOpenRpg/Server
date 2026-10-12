#include "client.h"
#include "npc.h"
#include "groups.h"
#include "raids.h"
#include "worldserver.h"
#include "corpse.h"
#include "beacon.h"
#include "aa.h"
#include "string_ids.h"
#include "../common/skill_caps.h"
#include "player_bot.h"
#include "player_bot_backattack.h"
#include "player_bot_rest.h"
#include "player_bot_follow.h"
#include "player_bot_formation.h"
#include "player_bot_formation_navigation.h"
#include "zone.h"
#include "water_map.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <memory>
#include <vector>
#include <map>
#include <set>
extern volatile bool is_zone_loaded;
extern WorldServer worldserver;
namespace {
class PlayerBot final : public NPC {
public:
 using NPC::SetAppearance;
 bool forced_sit_=false, resume_follow_=false;
 bool Retreating() const {auto it=options_.find("retreat");return it!=options_.end() && it->second!=0;}
 void BeginRetreat() {
  ForceSit(false);
  InterruptSpell();bardsong=0;
  WipeHateList();SetTarget(nullptr);
  ResetFormationMotion();StopNavigation();Follow();
  SetAppearance(eaStanding);
  rest_gate_.Reset(Timer::GetCurrentTime());rest_started_=0;
  if(auto* pet=GetPet()) {
   pet->InterruptSpell();pet->WipeHateList();pet->SetTarget(nullptr);pet->StopNavigation();
  }
 }
 void AI_Process() override { if(!forced_sit_) NPC::AI_Process(); }
 void ForceSit(bool enabled) {
  if(enabled==forced_sit_) return;
  if(enabled) {
   resume_follow_=GetFollowID()!=0;
   forced_sit_=true;
   InterruptSpell();bardsong=0;
   ResetFormationMotion();Stay();
   SetAppearance(eaSitting);
   if(auto* pet=GetPet()){pet->WipeHateList();pet->SetTarget(nullptr);pet->StopNavigation();}
  } else {
   forced_sit_=false;
   SetAppearance(eaStanding);
   if(resume_follow_) Follow(); else Stay();
   rest_gate_.Reset(Timer::GetCurrentTime());rest_started_=0;
  }
 }
 uint64 trace_start_=Timer::GetCurrentTime(), trace_next_=0;
 unsigned movement_packets_=0;
 unsigned trace_packets_=0;
 void TraceFollow(Client* owner) {
#ifndef PLAYER_BOT_DIAGNOSTICS
 return;
#endif
  const auto now=Timer::GetCurrentTime();
  if(now<trace_next_ || now-trace_start_>300000) return;
  trace_next_=now+50;
  auto* file=std::fopen("/tmp/takp-follow-trace.log","a");
  if(!file) return;
  std::fprintf(file,"%llu bot=%u owner=%.2f,%.2f botpos=%.2f,%.2f ownerdelta=%.3f,%.3f botdelta=%.3f,%.3f moving=%d casting=%d engaged=%d follow=%u inside=%d speed=%.3f sampled=%.3f packets=%u\n",static_cast<unsigned long long>(now),bot_id_,owner->GetX(),owner->GetY(),GetX(),GetY(),owner->GetDeltaX(),owner->GetDeltaY(),GetDeltaX(),GetDeltaY(),IsMoving(),IsCasting(),IsEngaged(),GetFollowID(),owner->GetInside(GetID()),GetCurrentSpeed(),follow_motion_.speed,movement_packets_);
  std::fclose(file);
 }
 BotFollow::Motion follow_motion_;
 BotFollow::PaceFilter follow_pace_filter_;
 bool follow_moving_=false;
 bool NormalFollow(Client* owner);
 int FollowPace(int maximum);
 bool FormationFollow(Client* owner);
 bool AtFormationRest();
 bool owner_was_moving_=false;
 bool arrival_facing_pending_=false;
 float arrival_facing_=0;
 void CheckFollowStop(Client* owner);
 void ResetFormationMotion() {
     if ((formation_moving_ || follow_moving_) && !IsEngaged() && !IsCasting() && !IsFeared()) StopNavigation();
     formation_moving_ = false;
     follow_moving_ = false;
     follow_pace_filter_.Reset();
     arrival_facing_pending_=false;
 }
 bool formation_moving_ = false;
 PlayerBotRestGate rest_gate_;
	PlayerBot(NPCType *type, Client *owner, uint32 id, uint32 generation)
		: NPC(type, nullptr, owner->GetPosition(), GravityBehavior::Water),
		  owner_entity_id_(owner->GetID()), owner_character_id_(owner->CharacterID()), bot_id_(id), generation_(generation)
	{
		GiveNPCTypeData();
		// Player spawns must match the group roster exactly, without NPC suffixes.
		strn0cpy(name, type->name, sizeof(name));
		SetOwnerID(owner->GetID());
		for (int skill=0;skill<=EQ::skills::HIGHEST_SKILL;++skill) SetSkill(static_cast<EQ::skills::SkillType>(skill),SkillCaps::Instance()->GetSkillCap(GetClass(),static_cast<EQ::skills::SkillType>(skill),GetLevel()).cap);
		SetBaseHP(GetLevel()*(GetClassLevelFactor()/10)*(300+GetSTA())/300);
		SetHP(GetMaxHP());
		SetSpecialAbility(SpecialAbility::CharmImmunity, 1);
		SetSpecialAbility(SpecialAbility::AggroImmunity, 0);
		SetSpecialAbility(SpecialAbility::BeingAggroImmunity, 0);
		Follow();
	}

	~PlayerBot() override
	{
		// Zone teardown deletes all groups after all mobs. Do not dereference
		// other group members while that bulk destruction is in progress.
		if (is_zone_loaded) { DismissPet(); LeaveCompanionGroup(); }
	}

 void FillSpawnStruct(NewSpawn_Struct *ns,Mob *viewer) override {NPC::FillSpawnStruct(ns,viewer);
  // Match client spawns: player races use each equipped armor material.
  if(IsPlayerRace(GetRace())) ns->spawn.bodytexture=0xFF;
  ns->spawn.NPC=0;ns->spawn.is_pet=0;ns->spawn.petOwnerId=0;ns->spawn.temporaryPet=0;ns->spawn.lastName[0]=0;ns->spawn.runspeed=static_cast<int>(ns->spawn.runspeed*40+4)/8*0.1f;ns->spawn.walkspeed=static_cast<int>(ns->spawn.walkspeed*40+4)/8*0.1f;}
 int32 CalcMaxMana() override;
 int32 PlayerHPRegen(){return Client::LevelRegen(GetLevel(),GetAppearance()==eaSitting,rest_started_&&Timer::GetTimeSeconds()-rest_started_>=60,false,false,(GetPlayerRaceBit(GetBaseRace())&RuleI(Character,BaseHPRegenBonusRaces))!=0)+aabonuses.HPRegen;}
 int32 PlayerManaRegen(){if(GetMaxMana()==0)return 0;int regen=PlayerBotBaseManaRegen(GetAppearance()==eaSitting,GetClass()==Class::Bard,GetSkill(EQ::skills::SkillMeditate));return regen+(GetLevel()>61)+(GetLevel()>63)+aabonuses.ManaRegen+spellbonuses.ManaRegen+itembonuses.ManaRegen+(GetBuffSlotFromType(SE_CompleteHeal)>=0?1:0);}
 int GetMaxBuffSlots() const override {return std::min(NPC::GetMaxBuffSlots(),NPC::GetMaxTotalSlots()-1);}
	Group *GetGroup() override { return entity_list.GetGroupByMob(this); }
	bool HasGroup() override { return GetGroup() != nullptr; }
 Raid *GetRaid() override {return raid_id_?entity_list.GetRaidByID(raid_id_):nullptr;}
 bool HasRaid() override {return GetRaid()!=nullptr;}

	void LeaveCompanionGroup()
	{
		LeaveRaid();
		if (auto *group = GetGroup()) group->DelMember(this);
		SetGrouped(false);
	}

	bool JoinOwnerGroup(Client *owner)
	{
		if (!BelongsTo(owner)) return false;
		if (owner->HasRaid()) return JoinRaid(owner);
		auto *group = owner->GetGroup();
		if (GetGroup()) return GetGroup() == group;
		if (group && (!group->IsLeader(owner) || group->GroupCount() >= MAX_GROUP_MEMBERS)) return false;
		bool created = false;
		if (!group) {
			group = new Group(owner);
			entity_list.AddGroup(group);
			if (!group->GetID()) {
				// AddGroup does not register or delete the group when IDs run out.
				owner->SetGrouped(false);
				std::memset(owner->GetPP().groupMembers, 0, sizeof(owner->GetPP().groupMembers));
				delete group;
				return false;
			}
			created = true;
			owner->UpdateGroupID(group->GetID());
			database.SetGroupLeaderName(group->GetID(), owner->GetName());
			database.SetGroupOldLeaderName(group->GetID(), owner->GetName());
			auto *packet = new EQApplicationPacket(OP_GroupUpdate, sizeof(GroupJoin_Struct));
			auto *join = reinterpret_cast<GroupJoin_Struct *>(packet->pBuffer);
			strn0cpy(join->membername, owner->GetName(), sizeof(join->membername));
			strn0cpy(join->yourname, owner->GetName(), sizeof(join->yourname));
			join->action = groupActInviteInitial;
			owner->QueuePacket(packet);
			delete packet;
		}
		if (!group->AddMember(this)) {
			if (created) group->DisbandGroup();
			return false;
		}
		group->SendHPPacketsTo(owner);
		return true;
	}

	bool BelongsTo(Client *client) const
	{
		return client && client->GetID() == owner_entity_id_ &&
			client->CharacterID() == owner_character_id_;
	}

	bool Process() override
	{
		if (GetDepop()) return false;
  if(active_disc_&&disc_expires_&&Timer::GetTimeSeconds()>=disc_expires_)FadeDiscipline();
		auto *owner = entity_list.GetClientByID(owner_entity_id_);
		if (!BelongsTo(owner) || !owner->Connected() || owner->IsDead()) {
			Save(!owner || !owner->IsDead());
			DismissPet();
			LeaveCompanionGroup();
			Depop();
			return false;
		}
		if (auto *group = GetGroup()) {
			if (group != owner->GetGroup() || owner->HasRaid()) LeaveCompanionGroup();
			else if (group_hp_timer_.Check()) group->SendHPPacketsFrom(this);
		}
  if(auto *raid=GetRaid()) {
   if(raid!=owner->GetRaid()||!raid->IsRaidMember(GetName())) {Save(false);LeaveCompanionGroup();DismissPet();Depop();return false;}
   if(group_hp_timer_.Check())raid->SendHPPacketsFrom(this);
  }
  else if(owner->HasRaid()&&!GetGroup()&&!JoinRaid(owner)){Save(false);DismissPet();Depop();return false;}
		if (save_timer_.Check() && !Save(true)) { LeaveCompanionGroup(); Depop(); return false; }
		if (auto *pet=GetPet()) {
			if (!stance_ || (pet->GetTarget() && !Allowed(pet->GetTarget()))) { pet->WipeHateList(); pet->SetTarget(nullptr); }
		}
		if(!IsEngaged()&&!owner->GetAggroCount()&&GetFollowID()&&DistanceSquared(GetPosition(),owner->GetPosition())>250000)GMMove(owner->GetX(),owner->GetY(),owner->GetZ(),owner->GetHeading());
		if (assist_timer_.Check()) UpdateAssist();
  follow_motion_.Update(Timer::GetCurrentTime(),owner->GetX(),owner->GetY(),owner->GetDeltaX(),owner->GetDeltaY());
  CheckFollowStop(owner);
  const bool can_rest=!Retreating()&&!IsEngaged()&&!owner->GetAggroCount()&&!IsCasting()&&PlayerBotRestLocation(GetFollowID()!=0,DistanceSquaredNoZ(GetPosition(),owner->GetPosition()),GetFollowDistance(),AtFormationRest())&&(GetMana()<GetMaxMana()||GetHP()<GetMaxHP());
  const bool owner_moving=follow_motion_.Moving(Timer::GetCurrentTime());
  const bool resting=forced_sit_ || rest_gate_.Update(Timer::GetCurrentTime(),
      {owner->GetX(),owner->GetY(),owner->GetZ()}, {GetX(),GetY(),GetZ()},
      IsMoving()||owner_moving,can_rest);
  if(resting){if(!rest_started_)rest_started_=Timer::GetTimeSeconds();if(GetAppearance()!=eaSitting)SetAppearance(eaSitting);}
  else{rest_started_=0;if(GetAppearance()==eaSitting)SetAppearance(eaStanding);}
		// Mac clients expire player models after 10-12 seconds without movement.
		// Keep this independent of casting, combat, resting and group membership.
		if (position_heartbeat_.Check()) {
			EQApplicationPacket packet(OP_MobUpdate, sizeof(SpawnPositionUpdates_Struct));
			auto *update = reinterpret_cast<SpawnPositionUpdates_Struct*>(packet.pBuffer);
			update->num_updates = 1;
			MakeSpawnUpdate(&update->spawn_update);
			// Like Client's heartbeat, do not depend on the NPC proximity cache.
			entity_list.QueueClients(this, &packet);
		}
		const bool alive = NPC::Process();
        if(alive) TraceFollow(owner);
        // The AI can start a new path/cast after the pre-process resting check.
        if (alive && !forced_sit_ && (IsMoving() || IsCasting() || owner_moving)) {
            rest_gate_.Reset(Timer::GetCurrentTime());
            rest_started_=0;
            if (GetAppearance()==eaSitting) SetAppearance(eaStanding);
        }
		if (!alive) LeaveCompanionGroup();
		return alive;
	}

	bool Attack(Mob *target, int hand = EQ::invslot::slotPrimary, int damage = 100) override
	{
		return stance_ && Allowed(target) && NPC::Attack(target, hand, damage);
	}
 bool BackAttackEnabled() const {auto it=options_.find("backattack");return it!=options_.end() && it->second!=0;}
 bool BackAttackPosition();
 bool backattack_moving_=false;
 uint64 backattack_next_=0;
 bool CloseTankPosition();
 bool tank_close_moving_=false;
 uint64 tank_close_next_=0;
 Timer bot_taunt_timer_{6000};
 unsigned bot_taunt_attempts_=0;
 void DoClassAttacks(Mob *target) override {
  if(!stance_ || options_["suspend"] || !Allowed(target) || IsCasting() || IsStunned() || IsMezzed() || IsFeared() || DivineAura() || !CombatRange(target)) return;
  if(IsTaunting() && GetSkill(EQ::skills::SkillTaunt)>0 && target->IsNPC() &&
     !IsCasting() && !IsStunned() && !IsMezzed() && !IsFeared() && !DivineAura() &&
     CombatRange(target) && bot_taunt_timer_.Check()) {
   ++bot_taunt_attempts_;
   Taunt(target->CastToNPC(),false);
  }
  NPC::DoClassAttacks(target);
 }

	bool Death(Mob *, int32, uint16, EQ::skills::SkillType, uint8 = 0, bool = false) override
	{
		SetHP(0); Save(false);
		DismissPet();
		LeaveCompanionGroup();
		Depop();
		return true;
	}

	void Follow() { if(!forced_sit_) SetFollowID(owner_entity_id_); }
	void Stay()
	{
		SetFollowID(0);
		StopNavigation();
		SaveGuardSpot();
	}

 Client *Owner() const { auto *c = entity_list.GetClientByID(owner_entity_id_); return BelongsTo(c) ? c : nullptr; }
 Mob *GetOwner() override { return Owner(); }
 Mob *GetOwnerOrSelf() override { auto *c = Owner(); return c ? static_cast<Mob*>(c) : this; }
 bool HasOwner() override { return Owner() != nullptr; }
 bool IsPet() override { return false; }

 int AssistSourceID() const {auto it=options_.find("assist_source");return it==options_.end()?0:it->second;}
 Mob *ResolveAssistSource(int id) {
  if(id>0) return entity_list.GetClientByCharID(id);
  if(id<0) for(const auto &entry:entity_list.GetNPCList()) {
   auto *bot=dynamic_cast<PlayerBot*>(entry.second);
   if(bot && !bot->GetDepop() && static_cast<int64>(bot->ID())==-static_cast<int64>(id)) return bot;
  }
  return nullptr;
 }
 bool ValidAssistSource(Mob *source) {
  if(!source || source==this || source->HasDied() || !(source->IsClient() || IsPlayerBot(source))) return false;
  if(source->IsClient() && !source->CastToClient()->Connected()) return false;
  return source==Owner() || (GetGroup() && GetGroup()==source->GetGroup()) ||
         (GetRaid() && GetRaid()==source->GetRaid());
 }
 bool AssistCycle(Mob *source) {
  std::set<Mob*> visited{this};
  while(auto *bot=dynamic_cast<PlayerBot*>(source)) {
   if(!visited.insert(bot).second)return true;
   source=bot->ResolveAssistSource(bot->AssistSourceID());
  }
  return false;
 }
 Mob *AssistSource() {
  auto *source=ResolveAssistSource(AssistSourceID());
  return ValidAssistSource(source) && !AssistCycle(source)?source:nullptr;
 }
 Mob *AssistEnemy() {auto *source=AssistSource();return source?source->GetTarget():nullptr;}
 void ClearAssistCombat() {
  if(IsCasting() && IsValidSpell(casting_spell_id) && !IsBeneficialSpell(casting_spell_id)) InterruptSpell();
  WipeHateList();SetTarget(nullptr);StopNavigation();
  if(auto *pet=GetPet()) {pet->WipeHateList();pet->SetTarget(nullptr);pet->StopNavigation();if(pet->IsCasting())pet->InterruptSpell();}
 }
 bool SetAssistSource(Mob *source) {
  int id=0;
  if(source) {
   if(!ValidAssistSource(source) || AssistCycle(source))return false;
   if(auto *bot=dynamic_cast<PlayerBot*>(source)) {if(bot->ID()>2147483647u)return false;id=-static_cast<int>(bot->ID());}
   else {if(source->CastToClient()->CharacterID()>2147483647u)return false;id=source->CastToClient()->CharacterID();}
  }
  if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_options(bot_id,setting,value) VALUES({},'assist_source',{}) ON DUPLICATE KEY UPDATE value=VALUES(value)",bot_id_,id)).Success())return false;
  options_["assist_source"]=id;ClearAssistCombat();return true;
 }
 void UpdateAssist() {
  if(!AssistSourceID()) {
   auto *owner=Owner();
   if(owner && stance_ && !IsEngaged() && owner->AutoAttackEnabled() && Allowed(owner->GetTarget()))Engage(owner->GetTarget());
   return;
  }
  auto *enemy=AssistEnemy();
  if(!stance_ || !Allowed(enemy)) {if(GetTarget() || IsEngaged() || (GetPet() && GetPet()->GetTarget()))ClearAssistCombat();return;}
  if(GetTarget()!=enemy || !IsEngaged()) {ClearAssistCombat();Engage(enemy);}
 }

 bool Allowed(Mob *target) {
  if(AssistSourceID() && target!=AssistEnemy())return false;
  auto *c = Owner();
  return c && !Retreating() && !forced_sit_ && !options_["suspend"] && target && target != this && target->IsNPC() && !IsPlayerBot(target) && !target->IsPlayerOwned() &&
   !target->IsMezzed() && !target->HasDied() && c->IsAttackAllowed(target) && DistanceSquared(GetPosition(),target->GetPosition()) < 40000;
 }
 bool IsAttackAllowed(Mob *target, bool spell = false, int16 spell_id = 0) override { return Allowed(target); }
 void Engage(Mob *target) { if (Allowed(target)) { stance_=1; AddToHateList(target, 1); SetTarget(target); if(auto *pet=GetPet()){pet->AddToHateList(target,1);pet->SetTarget(target);} } }
 uint32 Generation() const {return generation_;}
 uint32 ID() const { return bot_id_; }
 void Passive() { stance_ = 0; WipeHateList(); SetTarget(nullptr); InterruptSpell(); if(auto *pet=GetPet()){pet->WipeHateList();pet->SetTarget(nullptr);} Follow(); }
 void Assist() { stance_ = 1; }
 bool Save(bool active);
 bool LoadSpellState();
 // Negative saved resources mean a fresh spawn or a full-resource revival.
 void Restore(int hp, int mana, int stance) { SetHP(hp < 0 ? GetMaxHP() : std::min<int>(GetMaxHP(),hp)); SetMana(mana < 0 ? GetMaxMana() : std::min<int>(GetMaxMana(),mana)); stance_=stance; }
 bool AI_EngagedCastCheck() override {
  if(CastClassSpells(true))return true;
  if(CanRangedAttack(GetTarget()) && DistanceSquared(GetPosition(),GetTarget()->GetPosition())>100) {StopNavigation();RangedAttack(GetTarget());return true;}
  return false;
 }
 bool AI_IdleCastCheck() override { return CastClassSpells(false); }
 bool CastClassSpells(bool combat);
 bool CastKnown(uint16 id,Mob *target);
 void ListDepartures();
 bool Evacuate();
 bool HealTarget(Mob *target);
 void LoadClassSpells();
 void LoadEquipment();
 bool LoadFeatures();
 int32 GetSTR() const override {return Mob::GetSTR()+aabonuses.STR;}
 int32 GetSTA() const override {return Mob::GetSTA()+aabonuses.STA;}
 int32 GetDEX() const override {return Mob::GetDEX()+aabonuses.DEX;}
 int32 GetAGI() const override {return Mob::GetAGI()+aabonuses.AGI;}
 int32 GetINT() const override {return Mob::GetINT()+aabonuses.INT;}
 int32 GetWIS() const override {return Mob::GetWIS()+aabonuses.WIS;}
 int32 GetCHA() const override {return Mob::GetCHA()+aabonuses.CHA;}
 int32 GetMR() const override {return Mob::GetMR()+aabonuses.MR;}
 int32 GetFR() const override {return Mob::GetFR()+aabonuses.FR;}
 int32 GetCR() const override {return Mob::GetCR()+aabonuses.CR;}
 int32 GetPR() const override {return Mob::GetPR()+aabonuses.PR;}
 int32 GetDR() const override {return Mob::GetDR()+aabonuses.DR;}
 bool LoadConfiguration();
 void LoadAAs();
 void ApplyAABonuses(uint32 aaid,uint32 slots,StatBonuses *newbon);
 void CalcBonuses() override;
 int32 GetAACastingTimeModifier(uint16 spell_id,int32 casttime);
 int16 CalcAAFocus(focusType type,uint32 aa_ID,uint16 spell_id);
 bool Projects(uint16 id,Mob *target,bool consume=false){if(!IsValidSpell(id)||!IsPlayerIllusionSpell(id)||project_illusion_until_<=Timer::GetTimeSeconds()||!target||!(target->IsClient()||IsPlayerBot(target))||!Friendly(target))return false;if(consume)project_illusion_until_=0;return true;}
 uint32 GetAA(uint32 id) const {auto r=aa_ranks_.find(id);return r==aa_ranks_.end()?0:r->second;}
 bool ClaimAbility(const std::string &kind,uint32 id,uint32 seconds);
 bool UseDiscipline(uint8 id);
 bool CastDiscipline(uint8 id,uint8 level);
 uint8 DisciplineUseLevel(uint8 id);
 uint8 GetDiscTimerID(uint8 id);
 bool InstantDisc(uint16 skill) const {return (GetClass()==Class::Monk&&((skill==EQ::skills::SkillFlyingKick&&active_disc_==disc_thunderkick)||(skill==EQ::skills::SkillEagleStrike&&active_disc_==disc_ashenhand)||(skill==EQ::skills::SkillDragonPunch&&active_disc_==disc_silentfist)))||(GetClass()==Class::ShadowKnight&&active_disc_==disc_unholyaura&&(skill==SPELL_HARM_TOUCH||skill==SPELL_HARM_TOUCH2||skill==SPELL_IMP_HARM_TOUCH));}
 uint8 ActiveDisc() const {return active_disc_;}
 void FadeDiscipline(){if(active_disc_spell_)BuffFadeBySpellID(active_disc_spell_);active_disc_=0;active_disc_spell_=0;disc_expires_=0;}
 bool ActivateAA(uint32 id,Mob *target);
 void ListAbilities();
 bool ConsumeMassBuff(uint16 spell){if(mass_buff_until_<=Timer::GetTimeSeconds()||!IsMGBCompatibleSpell(spell))return false;mass_buff_until_=0;return true;}
 bool SetSpellSetting(uint16 id,int enabled,int min_hp=0,int max_hp=100,int priority=0);
 bool SetBehavior(const std::string &key,int value);
 static bool ValidAppearance(uint16 race,uint8 gender,const std::string &key,int value){
  if(value<0||value>=255)return false;
  if(key=="face")return RaceAppearance::IsValidFace(race,gender,value);
  if(key=="hair")return RaceAppearance::IsValidHair(race,gender,value);
  if(key=="haircolor")return RaceAppearance::IsValidHairColor(race,gender,value);
  if(key=="beard")return RaceAppearance::IsValidBeard(race,gender,value);
  if(key=="beardcolor")return RaceAppearance::IsValidBeardColor(race,gender,value);
  if(key=="eye1"||key=="eye2")return RaceAppearance::IsValidEyeColor(race,gender,value);
  return false;
 }
 bool SetAppearance(const std::string &key,int value){
  if(!ValidAppearance(GetBaseRace(),GetBaseGender(),key,value))return false;
  return database.QueryDatabase(fmt::format("INSERT INTO takp_bot_options VALUES({},'appearance_{}',{}) ON DUPLICATE KEY UPDATE value=VALUES(value)",bot_id_,key,value)).Success();
 }

 bool BlockBuff(uint16 id,bool blocked);
 bool IsImmuneToSpell(uint16 id,Mob *caster,bool proc=false) override {return (warcry_until_>Timer::GetTimeSeconds()&&IsEffectInSpell(id,SE_Fear))||blocked_buffs_.count(id)||NPC::IsImmuneToSpell(id,caster,proc);}
 bool RotationHeal(bool &configured);
 bool SpellPermitted(uint16 id,Mob *target);
 bool ClickItem(int slot,Mob *target);
 void ListSpells();
 bool SavePetState();
 bool RestorePet();
 void DismissPet() { if(auto *pet=GetPet()){pet->WipeHateList();if(pet->IsCharmedPet())pet->BuffFadeByEffect(SE_Charm);else pet->Depop();SetPetID(0);} }
 bool Friendly(Mob *target);
 int32 BotDoTDamage(uint16 id,int32 value) {
  value+=value*Focus(focusImprovedDamage,id)/100;
  if(zone->random.Roll(itembonuses.CriticalDoTChance+spellbonuses.CriticalDoTChance+aabonuses.CriticalDoTChance))value*=2;
  return value;
 }
 std::vector<Mob*> Friends();
 bool JoinRaid(Client *owner);
 void LeaveRaid();
 bool Utility(const std::string &kind, Mob *target);
 bool Cure(Mob *target);
 bool ControlAdds();
 bool TrySpell(uint16 id,Mob *target);
 bool CastSpell(uint16 spell_id,uint16 target_id,EQ::spells::CastingSlot slot=EQ::spells::CastingSlot::Item,int32 casttime=-1,int32 mana_cost=-1,uint32 *finish=nullptr,uint32 item_slot=0xFFFFFFFF,uint32 timer=0xFFFFFFFF,uint32 duration=0,uint32 type=0,int16 *resist=nullptr) override;
 bool Components(uint16 id,bool consume);
 bool ConsumeComponents(uint16 id);
 bool SetOption(const std::string &option,int value);
 bool DirectDamageEnabled() const {auto s=options_.find("dpscast");return s==options_.end() || s->second!=0;}
 bool DirectDamageAllowed(uint16 id) const {
  auto setting=options_.find("dpscast");
  return setting==options_.end() || setting->second!=0 || !IsValidSpell(id) ||
         !IsDetrimentalSpell(id) || !IsDamageSpell(id) || IsDOTSpell(id);
 }

 bool SetSongs(const std::vector<uint16> &ids);
 uint32 EquippedItem(int slot) const {return slot==EQ::invslot::slotAmmo?ammo_item_:(slot>=EQ::invslot::EQUIPMENT_BEGIN&&slot<EQ::invslot::EQUIPMENT_COUNT?equipment[slot]:0);}
 bool CanRangedAttack(Mob *target);
 void RangedAttack(Mob *target) override;
 bool WantsRanged() const {return ranged_mode_;}
 bool Knows(uint16 id) const {return IsValidSpell(id)&&!spells[id].not_player_spell&&spells[id].classes[GetClass()-1]>0&&spells[id].classes[GetClass()-1]<=GetLevel();}
 void RestoreBuffEffects();
 int GetBaseDamage(Mob *defender=nullptr,int slot=EQ::invslot::slotPrimary) override;
 int GetDamageBonus() override;
 int GetOffense(EQ::skills::SkillType skill) override;
 int GetToHit(EQ::skills::SkillType skill) override;
 int GetMitigation() override;
 int32 CalcMaxHP(bool unbuffed=false) override;
 int16 Focus(focusType type,uint16 id);
 int32 GetActSpellHealing(uint16 id,int32 value,Mob *target=nullptr,bool hot=false) override;
 int32 GetActSpellDamage(uint16 id,int32 value,Mob *target=nullptr) override;
 float GetSpellRange(uint16 id,float range) override {return range*(100+Focus(focusRange,id))/100;}
 float GetActSpellRange(uint16 id,float range,std::string&) override {return range*(100+Focus(focusRange,id))/100;}
 int32 GetActSpellCost(uint16 id,int32 cost) override {return std::max<int32>(0,cost*(100-Focus(focusManaCost,id))/100)*(mass_buff_until_>Timer::GetTimeSeconds()&&IsMGBCompatibleSpell(id)?2:1);}
 int32 GetActSpellDuration(uint16 id,int32 duration) override {return duration*(100+Focus(focusSpellDuration,id))/100;}
 int32 GetActSpellCasttime(uint16 id,int32 time) override {return std::max(time/2,time+GetAACastingTimeModifier(id,time)-time*Focus(focusSpellHaste,id)/100);}

 void SetAttackTimer(bool trigger=false) override;
 int GetHandToHandDamage();
 int GetHandToHandDelay();
 uint32 RollDamageMultiplier(uint32 offense,int &damage,EQ::skills::SkillType skill);
private:
 bool ranged_mode_=false,pet_enabled_=true;
 uint32 raid_id_=0;
 std::map<uint32,uint32> aa_ranks_;
 uint8 active_disc_=0;
 uint16 active_disc_spell_=0;
 uint32 disc_expires_=0;
 uint32 rest_started_=0;
 uint32 mass_buff_until_=0,project_illusion_until_=0,warcry_until_=0;
 uint32 ammo_item_=0;
 struct SpellSetting {int enabled,min_hp,max_hp,priority;};
 std::map<uint16,SpellSetting> spell_settings_;
 std::map<std::string,int> options_;
 std::set<uint16> blocked_buffs_;
 int pending_click_slot_=-1;
 uint16 pending_click_spell_=0;
 std::vector<uint16> utility_spells_,songs_;
 std::map<uint16,uint32> utility_recasts_;
 size_t next_song_=0;
 Timer ranged_bot_timer_{3000};
 uint32 bot_id_, generation_;
 int stance_ = 1;
 Timer save_timer_{5000};
 Timer assist_timer_{500};
 Timer spell_timer_{1500};
	uint16 owner_entity_id_;
	uint32 owner_character_id_;
	Timer group_hp_timer_{1000};
	Timer position_heartbeat_{2000};
};

bool PlayerBot::Save(bool active)
{
 if(!database.QueryDatabase("START TRANSACTION").Success()) return false;
 auto fail=[](){database.QueryDatabase("ROLLBACK");return false;};
 auto lease=database.QueryDatabase(fmt::format("SELECT generation FROM takp_bot_runtime WHERE bot_id={} FOR UPDATE",bot_id_));
 if(!lease.Success()||lease.RowCount()!=1||strtoul(lease.begin()[0],nullptr,10)!=generation_) return fail();
 if(!database.QueryDatabase(fmt::format("UPDATE takp_bot_runtime SET active={},hp={},mana={},stance={} WHERE bot_id={}",active,GetHP(),GetMana(),stance_,bot_id_)).Success()) return fail();
 if(!database.QueryDatabase(fmt::format("DELETE FROM takp_bot_buffs WHERE bot_id={}",bot_id_)).Success()) return fail();
 if(GetHP()>0) for(int slot=0;slot<GetMaxTotalSlots();++slot) {
  const auto &b=buffs[slot]; if(!IsValidSpell(b.spellid)||IsDisc(b.spellid)) continue;
  if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_buffs VALUES({},{},{},{},{},{},{},{},{})",bot_id_,slot,b.spellid,b.casterlevel,b.ticsremaining,b.counters,b.melee_rune,b.magic_rune,b.instrumentmod)).Success()) return fail();
 }
 if(!database.QueryDatabase(fmt::format("DELETE FROM takp_bot_recasts WHERE bot_id={}",bot_id_)).Success()) return fail();
 for(const auto &spell:AIspells) if(spell.time_cancast>Timer::GetCurrentTime()) {
  const auto ms=spell.time_cancast-Timer::GetCurrentTime();
  if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_recasts VALUES({},{},TIMESTAMPADD(MICROSECOND,{},CURRENT_TIMESTAMP(3)))",bot_id_,spell.spellid,static_cast<uint64>(ms)*1000)).Success()) return fail();
 }
 for(const auto &entry:utility_recasts_) if(entry.second>Timer::GetCurrentTime()) {
  if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_recasts VALUES({},{},TIMESTAMPADD(MICROSECOND,{},CURRENT_TIMESTAMP(3))) ON DUPLICATE KEY UPDATE available_at=GREATEST(available_at,VALUES(available_at))",bot_id_,entry.first,static_cast<uint64>(entry.second-Timer::GetCurrentTime())*1000)).Success()) return fail();
 }
 if(!SavePetState()) return fail();
 return database.QueryDatabase("COMMIT").Success();
}

bool PlayerBot::LoadSpellState()
{
 auto saved=database.QueryDatabase(fmt::format("SELECT slot,spell_id,caster_level,ticks,counters,melee_rune,magic_rune,instrument_mod FROM takp_bot_buffs WHERE bot_id={}",bot_id_));
 if(!saved.Success()) return false;
 for(auto r=saved.begin();r!=saved.end();++r) {
  int slot=atoi(r[0]),id=atoi(r[1]); if(slot<0||slot>=GetMaxTotalSlots()||!IsValidSpell(id)||IsDisc(id)) continue;
  auto &b=buffs[slot]; b={}; b.spellid=id; b.casterlevel=b.realcasterlevel=atoi(r[2]);
  b.ticsremaining=atoi(r[3]); b.counters=atoi(r[4]); b.melee_rune=strtoul(r[5],nullptr,10); b.magic_rune=strtoul(r[6],nullptr,10); b.instrumentmod=atoi(r[7]); b.bufftype=2;
 }
 auto recasts=database.QueryDatabase(fmt::format("SELECT spell_id,GREATEST(0,TIMESTAMPDIFF(MICROSECOND,CURRENT_TIMESTAMP(3),available_at) DIV 1000) FROM takp_bot_recasts WHERE bot_id={}",bot_id_));
 if(!recasts.Success()) return false;
 for(auto r=recasts.begin();r!=recasts.end();++r) {
  const auto until=Timer::GetCurrentTime()+strtoul(r[1],nullptr,10);
  utility_recasts_[atoi(r[0])]=until;
  for(auto &spell:AIspells) if(spell.spellid==atoi(r[0])) spell.time_cancast=until;
 }
 CalcBonuses(); RestoreBuffEffects();
 return true;
}

void PlayerBot::LoadClassSpells()
{
 // Build from the destination server's spell data, never hard-coded modern IDs.
 std::vector<int> ids;
 for (int id=1; id<SPDAT_RECORDS; ++id) {
  if (!IsValidSpell(id)) continue;
  const auto &s=spells[id];
  if (s.not_player_spell || !s.classes[GetClass()-1] || s.classes[GetClass()-1]>GetLevel()) continue;
  if (s.targettype!=ST_Target && s.targettype!=ST_Self && s.targettype!=ST_Group && s.targettype!=ST_Corpse && s.targettype!=ST_Pet) continue;
  if(IsCureSpell(id)||IsSummonPetSpell(id)||IsEffectInSpell(id,SE_Revive)||IsMezSpell(id)||IsCharmSpell(id)||IsFearSpell(id)||IsEffectInSpell(id,SE_Lull)||IsEffectInSpell(id,SE_Invisibility)||IsEffectInSpell(id,SE_InvisVsUndead)||IsEffectInSpell(id,SE_Levitate)||IsEffectInSpell(id,SE_WaterBreathing)) utility_spells_.push_back(id);
  if(IsCharmSpell(id) || IsFearSpell(id) || IsEvacSpell(id) || IsSummonSpell(id) || IsSummonPetSpell(id) || IsCureSpell(id) || IsMezSpell(id)) continue;
  ids.push_back(id);
 }
 std::sort(ids.begin(),ids.end(),[&](int a,int b){return spells[a].classes[GetClass()-1]<spells[b].classes[GetClass()-1];});
 for(int id:ids) {
  uint16 kind=0;
  if(IsHealingSpell(id) && !IsDamageSpell(id)) kind=SpellType_Heal;
  else if(IsSlowSpell(id)) kind=SpellType_Slow;
  else if(IsEffectInSpell(id,SE_Root)) kind=SpellType_Root;
  else if(IsEffectInSpell(id,SE_MovementSpeed)&&IsDetrimentalSpell(id)) kind=SpellType_Snare;
  else if(IsDOTSpell(id)) kind=SpellType_DOT;
  else if(IsPureNukeSpell(id)) kind=SpellType_Nuke;
  else if(IsLifetapSpell(id)) kind=SpellType_Lifetap;
  else if(IsBeneficialSpell(id) && !IsInvulnerabilitySpell(id) &&
   (IsHasteSpell(id) || IsEffectInSpell(id,SE_ArmorClass) || IsEffectInSpell(id,SE_TotalHP) || IsEffectInSpell(id,SE_ATK))) kind=SpellType_Buff;
  if(kind) AddSpellToNPCList(1,id,kind,-1,-1,spells[id].ResistDiff);
 }
 std::sort(utility_spells_.begin(),utility_spells_.end(),[&](int a,int b){return spells[a].classes[GetClass()-1]<spells[b].classes[GetClass()-1];});
 // NPC's casting index is uint8. Keep the highest-level entries for each type.
 if(AIspells.size()>255) AIspells.erase(AIspells.begin(),AIspells.end()-255);
}

bool PlayerBot::HealTarget(Mob *target)
{
 if(!target || target->HasDied() || target->GetHPRatio()>=(options_.count("healpercent")?options_["healpercent"]:85)) return false;
 // Prefer immediate recovery over repeatedly refreshing a heal-over-time buff.
 for(int periodic=0;periodic<2;++periodic) for(int i=static_cast<int>(AIspells.size())-1;i>=0;--i) {
  const auto &entry=AIspells[i]; const auto &spell=spells[entry.spellid];
  if(entry.type!=SpellType_Heal || (spell.buffduration>0)!=static_cast<bool>(periodic)) continue;
  if(entry.time_cancast>Timer::GetCurrentTime() || spell.mana>GetMana()) continue;
  if(spell.targettype==ST_Self && target!=this) continue;
  if(DistanceSquared(GetPosition(),target->GetPosition())>spell.range*spell.range) continue;
  if(periodic && target->CanBuffStack(entry.spellid,GetLevel(),true)<0) continue;
  if(i>255) continue; // TAKP's NPC casting index is eight bits.
  if(AIDoSpellCast(i,spell.targettype==ST_Group?this:target,spell.mana)) return true;
 }
 return false;
}

bool PlayerBot::CastClassSpells(bool combat)
{
 if(Retreating() || IsCasting() || !spell_timer_.Check() || !Owner() || options_["suspend"]) return false;
 Mob *hurt=this;
 if(Owner()->GetHPRatio()<hurt->GetHPRatio()) hurt=Owner();
 for(auto *m:Friends())
  if(m && !m->HasDied() && m->GetHPRatio()<hurt->GetHPRatio()) hurt=m;
 bool in_rotation=false;if(RotationHeal(in_rotation))return true;
 if(!in_rotation && hurt->GetHPRatio()<(options_.count("healpercent")?options_["healpercent"]:85) && HealTarget(hurt)) return true;
 if(Cure(hurt)) return true;
 for(auto *m:Friends()) if(m && Cure(m)) return true;
 if(combat && stance_ && ControlAdds()) return true;
 if(GetClass()==Class::Bard && !songs_.empty()) {
  for(size_t n=0;n<songs_.size();++n){auto id=songs_[next_song_++%songs_.size()];auto *target=IsBeneficialSpell(id)?static_cast<Mob*>(this):GetTarget();if(target&&TrySpell(id,target))return true;}
 }
 if(!combat && pet_enabled_ && !GetPet() && Utility("pet",this)) return true;
 if(combat && stance_ && Allowed(GetTarget()))
  return AICastSpell(GetTarget(),100,SpellType_Slow|SpellType_DOT|SpellType_Nuke|SpellType_Lifetap|SpellType_Snare);
 if(!combat || GetClass()==Class::Bard) {
  if(AICastSpell(Owner(),100,SpellType_Buff)) return true;
  for(auto *m:Friends()) if(m && AICastSpell(m,100,SpellType_Buff)) return true;
  return AICastSpell(this,100,SpellType_Buff);
 }
 return false;
}

void PlayerBot::LoadEquipment()
{
 auto rows=database.QueryDatabase(fmt::format("SELECT slot,item_id,charges FROM takp_bot_inventory WHERE bot_id={} ORDER BY slot",bot_id_));
 if(!rows.Success()) return;
 ClearLootItems();
 std::memset(equipment,0,sizeof(equipment));ammo_item_=0;
 for(auto row=rows.begin();row!=rows.end();++row) {
  const int slot=atoi(row[0]); const auto *item=database.GetItem(strtoul(row[1],nullptr,10));
  if(!item || slot<EQ::invslot::EQUIPMENT_BEGIN || slot>EQ::invslot::EQUIPMENT_END) continue;
  // TAKP NPC equipment excludes ammunition; keep that player slot separately.
  if(slot==EQ::invslot::slotAmmo){ammo_item_=item->ID;continue;}
  // Explicit slots avoid NPC auto-equip rules silently replacing rings/weapons.
  AddItem(item,atoi(row[2]),false);
  auto *loot=m_loot_items.back(); loot->equip_slot=slot; equipment[slot]=item->ID;
 }
 CalcBonuses(); SetAttackTimer();
 for(int slot=EQ::textures::textureBegin;slot<=EQ::textures::LastTexture;++slot) SendWearChange(slot,nullptr,true);
}

std::vector<PlayerBot*> Owned(Client *c)
{
 std::vector<PlayerBot*> out;
 for(const auto &e:entity_list.GetNPCList()) {
  auto *b=dynamic_cast<PlayerBot*>(e.second);
  if(b && b->BelongsTo(c) && !b->GetDepop()) out.push_back(b);
 }
 return out;
}

#include "player_bot_formation.inc"

// Short, model-aware melee adjustments; never navigate toward the target center.

bool PlayerBot::BackAttackPosition() {
 auto *target=GetTarget();
 if(!options_["backattack"] || Retreating() || forced_sit_ || !stance_ || options_["suspend"] || ranged_mode_ ||
    !Allowed(target) || IsCasting() || IsRooted() || IsStunned() || IsMezzed() || IsFeared() || IsBlind() ||
    !zone->zonemap || target->GetTarget()==this || DistanceSquaredNoZ(GetPosition(),target->GetPosition())>6400.f) {
  if(backattack_moving_) StopNavigation();backattack_moving_=false;return false;
 }
 std::vector<PlayerBot*> members;
 float body=BotFormation::BodyRadius(GetSize());
 for(const auto &entry:entity_list.GetNPCList()) {
  auto *b=dynamic_cast<PlayerBot*>(entry.second);
  if(!b || b->GetDepop() || !b->options_["backattack"] || b->GetTarget()!=target || !b->stance_ || b->Retreating() ||
     b->forced_sit_ || b->options_["suspend"] || b->ranged_mode_ || target->GetTarget()==b)continue;
  if(b!=this && !(GetGroup() && b->GetGroup()==GetGroup()) && !(GetRaid() && b->GetRaid()==GetRaid()))continue;
  members.push_back(b);body=std::max(body,BotFormation::BodyRadius(b->GetSize()));
 }
 std::sort(members.begin(),members.end(),[](PlayerBot*a,PlayerBot*b){return a->ID()<b->ID();});
 const auto it=std::find(members.begin(),members.end(),this);
 if(it==members.end())return false;
 const unsigned index=it-members.begin(),count=members.size();
 const float radius=BotBackAttack::Radius(target->GetBoundingRadius(),body,count);
 const auto slot=BotBackAttack::Slot(target->GetHeading(),index,count,radius);
 const float dz=GetZ()-target->GetZ();
 // Never trade melee reach for fitting more bodies into the arc.
 if(!CombatRange(target,radius*radius+dz*dz)) {if(backattack_moving_)StopNavigation();backattack_moving_=false;return false;}
 const float dx=GetX()-target->GetX(),dy=GetY()-target->GetY();
 if(std::hypot(dx-slot.x,dy-slot.y)<(backattack_moving_?.5f:1.2f) && BehindMob(target,GetX(),GetY())) {
  if(backattack_moving_)StopNavigation();backattack_moving_=false;if(!IsFacingTarget())FaceTarget(target);return false;
 }
 const auto now=Timer::GetCurrentTime();
 if(now<backattack_next_)return backattack_moving_;
 backattack_next_=now+250;
 const auto step=BotBackAttack::Step({dx,dy},BotBackAttack::Angle(target->GetHeading(),index,count),radius);
 glm::vec3 dest(target->GetX()+step.x,target->GetY()+step.y,GetZ());
 const float floor=zone->zonemap->FindBestZ(dest,nullptr,20.f);
 bool safe=floor!=BEST_Z_INVALID && std::isfinite(floor);
 if(safe) {
  dest.z=floor+GetZOffset();
  safe=std::abs(dest.z-GetZ())<=6.f && !(zone->HasWaterMap() && zone->watermap->InLiquid(dest)) && !zone->IsWaterZone(dest.z) &&
       BotFormation::SafeRoute(zone->pathing,zone->zonemap,glm::vec3(GetPosition()),dest);
 }
 if(!safe) {if(backattack_moving_)StopNavigation();backattack_moving_=false;return false;}
 RunTo(dest.x,dest.y,dest.z);backattack_moving_=true;return true;
}

bool PlayerBot::CloseTankPosition() {
 auto *target=GetTarget();
 if(options_["backattack"] || !options_["tankclose"] || Retreating() || forced_sit_ || !stance_ || options_["suspend"] || ranged_mode_ ||
    !Allowed(target) || IsCasting() || IsRooted() || IsStunned() || IsMezzed() || IsFeared() || IsBlind() ||
    !zone->zonemap || !CombatRange(target) || !CheckLosFN(target)) {tank_close_moving_=false;return false;}
 const float clearance=GetBoundingRadius()+target->GetBoundingRadius()+1.0f;
 // Large models whose clearance exceeds melee reach keep normal positioning.
 const float dz=GetZ()-target->GetZ();
 if(!std::isfinite(clearance) || !CombatRange(target,clearance*clearance+dz*dz)) {tank_close_moving_=false;return false;}
 float dx=GetX()-target->GetX(),dy=GetY()-target->GetY();
 const float distance=std::sqrt(dx*dx+dy*dy);
 const float tolerance=tank_close_moving_?0.25f:0.8f;
 if(std::abs(distance-clearance)<=tolerance) {
  if(tank_close_moving_) {StopNavigation();FaceTarget(target);}
  tank_close_moving_=false;return false;
 }
 const auto now=Timer::GetCurrentTime();
 if(now<tank_close_next_) return tank_close_moving_;
 tank_close_next_=now+300;
 // If already overlapping, step toward the owner's side rather than divide by zero.
 if(distance<0.01f) {auto *owner=Owner();dx=owner?owner->GetX()-target->GetX():1.f;dy=owner?owner->GetY()-target->GetY():0.f;}
 const float length=std::sqrt(dx*dx+dy*dy);
 if(length<0.01f) {dx=1.f;dy=0.f;} else {dx/=length;dy/=length;}
 glm::vec3 dest(target->GetX()+dx*clearance,target->GetY()+dy*clearance,GetZ());
 const float floor=zone->zonemap->FindBestZ(dest,nullptr,20.f);
 if(floor==BEST_Z_INVALID || !std::isfinite(floor)) {StopNavigation();tank_close_moving_=false;return false;}
 dest.z=floor+GetZOffset();
 if(std::abs(dest.z-GetZ())>6.f || (zone->HasWaterMap() && zone->watermap->InLiquid(dest)) || zone->IsWaterZone(dest.z) ||
    !BotFormation::SafeRoute(zone->pathing,zone->zonemap,glm::vec3(GetPosition()),dest)) {
  StopNavigation();tank_close_moving_=false;return false;
 }
 // LOS plus a direct segment prevents a detour through the enemy's center.
 if(!zone->zonemap->CheckLoS(glm::vec3(GetPosition()),dest)) {tank_close_moving_=false;return false;}
 RunTo(dest.x,dest.y,dest.z);tank_close_moving_=true;return true;
}


bool Spawn(Client *c,uint32 id)
{
 if(!c->Connected() || c->IsDead()) return false;
 for(auto *b:Owned(c)) if(b->ID()==id) return true;
 if(Owned(c).size()>=c->ChallengeBotLimit()) {
  c->Message(Chat::Yellow,"Bot limit reached: %u at your current level. Challenge unlocks: 5, 15, 25, 35, 45.",c->ChallengeBotLimit());return false;
 }
 auto rowset=database.QueryDatabase(fmt::format("SELECT name,class,race,gender FROM takp_bot_data WHERE id={} AND owner_character_id={}",id,c->CharacterID()));
 if(!rowset.Success() || rowset.RowCount()!=1) return false;
 if(!database.QueryDatabase(fmt::format("INSERT IGNORE INTO takp_bot_runtime(bot_id) VALUES({})",id)).Success()) return false;
 if(!database.QueryDatabase(fmt::format("UPDATE takp_bot_runtime SET generation=generation+1 WHERE bot_id={}",id)).Success()) return false;
 auto state=database.QueryDatabase(fmt::format("SELECT generation,hp,mana,stance FROM takp_bot_runtime WHERE bot_id={}",id));
 if(!state.Success() || state.RowCount()!=1) return false;
 auto r=rowset.begin(); auto s=state.begin();
 if(atoi(r[1])<1 || atoi(r[1])>15 || atoi(r[3])>1) return false;
 if(atoi(s[1])==0) { c->Message(Chat::White,"%s has fallen. Use #bot revive %s while out of combat.",r[0],r[0]); return false; }
 auto *type=new NPCType{};
 strn0cpy(type->name,r[0],sizeof(type->name)); type->class_=atoi(r[1]); type->race=atoi(r[2]); type->gender=atoi(r[3]);
 type->level=std::min<int>(65,c->GetLevel()); type->bodytype=1;
 type->cur_hp=type->max_hp=25+type->level*type->level*2;
 type->Mana=0; // Use TAKP class/stat mana calculation.
 type->STR=type->STA=type->DEX=type->AGI=type->INT=type->WIS=type->CHA=75;
 auto stats=database.QueryDatabase(fmt::format("SELECT a.base_str,a.base_sta,a.base_dex,a.base_agi,a.base_int,a.base_wis,a.base_cha FROM char_create_point_allocations a JOIN char_create_combinations c ON c.allocation_id=a.id WHERE c.race={} AND c.class={} ORDER BY c.deity,c.start_zone LIMIT 1",type->race,type->class_));
 if(stats.Success()&&stats.RowCount()){auto s=stats.begin();type->STR=atoi(s[0]);type->STA=atoi(s[1]);type->DEX=atoi(s[2]);type->AGI=atoi(s[3]);type->INT=atoi(s[4]);type->WIS=atoi(s[5]);type->CHA=atoi(s[6]);}
 type->AC=type->level*4; type->min_dmg=1+type->level/5; type->max_dmg=3+type->level*2;
 type->size=GetRaceGenderDefaultHeight(type->race,type->gender);
 type->runspeed=1.3f; type->attack_delay=30; type->hp_regen=0; type->mana_regen=0;
 type->spellscale=type->healscale=100; type->aggroradius=0; type->assistradius=0;
 auto appearance=database.QueryDatabase(fmt::format("SELECT setting,value FROM takp_bot_options WHERE bot_id={} AND setting LIKE 'appearance_%'",id));
 if(!appearance.Success()){delete type;return false;}
 for(auto a=appearance.begin();a!=appearance.end();++a){std::string key=std::string(a[0]).substr(11);int value=atoi(a[1]);if(!PlayerBot::ValidAppearance(type->race,type->gender,key,value))continue;
  if(key=="face")type->luclinface=value;else if(key=="hair")type->hairstyle=value;else if(key=="haircolor")type->haircolor=value;else if(key=="beard")type->beard=value;else if(key=="beardcolor")type->beardcolor=value;else if(key=="eye1")type->eyecolor1=value;else if(key=="eye2")type->eyecolor2=value;
 }
 auto *b=new PlayerBot(type,c,id,strtoul(s[0],nullptr,10));
 b->LoadClassSpells(); b->LoadAAs(); b->LoadEquipment();
 if(!b->LoadSpellState() || !b->LoadFeatures()) {delete b;return false;}
 b->Restore(atoi(s[1]),atoi(s[2]),atoi(s[3]));
 // Deliver the spawn before the group join so the client can resolve its name.
 entity_list.AddNPC(b,true,true);
 if(!b->JoinOwnerGroup(c)) { b->Depop(); c->Message(Chat::White,"Bots require a free slot in a group you lead."); return false; }
 if(!b->RestorePet()) {b->Depop();return false;}
 return b->Save(true);
}
#include "player_bot_features.inc"
#include "player_bot_stats.inc"
#include "player_bot_configuration.inc"
#include "player_bot_abilities.inc"
} // namespace

bool PlayerBotMassBuff(Mob *m,unsigned short spell){auto *b=dynamic_cast<PlayerBot*>(m);return b&&b->ConsumeMassBuff(spell);}
unsigned char PlayerBotDiscipline(Mob *m){auto *b=dynamic_cast<PlayerBot*>(m);return b?b->ActiveDisc():0;}
bool PlayerBotDirectDamageAllowed(Mob *m,unsigned short id){auto *b=dynamic_cast<PlayerBot*>(m);return !b || b->DirectDamageAllowed(id);}
bool PlayerBotSpellTargetAllowed(Mob *m,Mob *target,unsigned short id){
 auto *b=dynamic_cast<PlayerBot*>(m);if(!b)return true;
 if(!target || !IsValidSpell(id))return false;
 if(target==b)return true;
 if(IsHarmonySpell(id))return b->Allowed(target);
 if(IsBeneficialSpell(id) || IsNeutralSpell(id)) {
  if(b->Friendly(target) || b->IsBeneficialAllowed(target))return true;
  if(IsNeutralSpell(id))return b->Allowed(target);
  return false;
 }
 return b->Allowed(target);
}
int PlayerBotDoTDamage(Mob *m,unsigned short id,int value){auto *b=dynamic_cast<PlayerBot*>(m);return b?b->BotDoTDamage(id,value):value;}
bool HandlePlayerBotBackAttack(Mob *mob) {auto *bot=dynamic_cast<PlayerBot*>(mob);return bot && bot->BackAttackPosition();}
bool HandlePlayerBotTankClose(Mob *mob) {auto *bot=dynamic_cast<PlayerBot*>(mob);return bot && bot->CloseTankPosition();}
bool IsPlayerBot(Mob *m) { return dynamic_cast<PlayerBot*>(m)!=nullptr; }
bool IsPlayerBotPet(Mob *m) { return m && m->GetOwnerID() && IsPlayerBot(m->GetOwner()); }
bool ConsumePlayerBotReagents(Mob *m,unsigned short id){auto *b=dynamic_cast<PlayerBot*>(m);return b&&b->ConsumeComponents(id);}
void ApplyPlayerBotDamageMultiplier(Mob *mob,int offense,int &damage,int skill) { if(auto *bot=dynamic_cast<PlayerBot*>(mob))bot->RollDamageMultiplier(offense,damage,static_cast<EQ::skills::SkillType>(skill)); }
void SavePlayerBots(Client *c) { for(auto *b:Owned(c)) { b->Save(!c->IsDead()); b->DismissPet(); b->LeaveCompanionGroup(); b->Depop(); } }
void EnforceChallengeBotLimit(Client *c)
{
 if(!c)return;
 auto bots=Owned(c);
 std::sort(bots.begin(),bots.end(),[](PlayerBot *a,PlayerBot *b){return a->ID()<b->ID();});
 for(unsigned i=c->ChallengeBotLimit();i<bots.size();++i){
  auto *b=bots[i];
  if(!b->Save(false))c->Message(Chat::Red,"Could not save %s before enforcing the bot limit.",b->GetCleanName());
  b->DismissPet();b->LeaveCompanionGroup();b->Depop();
 }
 if(bots.size()>c->ChallengeBotLimit())c->Message(Chat::Yellow,"Excess bots dismissed to match your current bot limit (%u).",c->ChallengeBotLimit());
}
void RestorePlayerBots(Client *c)
{
 auto rows=database.QueryDatabase(fmt::format("SELECT d.id FROM takp_bot_data d JOIN takp_bot_runtime r ON r.bot_id=d.id WHERE d.owner_character_id={} AND r.active=1 ORDER BY d.id LIMIT {}",c->CharacterID(),c->ChallengeBotLimit()));
 if(rows.Success()) for(auto r=rows.begin();r!=rows.end();++r) Spawn(c,strtoul(r[0],nullptr,10));
}

namespace {
bool SlotNumber(const char *s,int &n) {
 if(!s || !*s) return false; n=0;
 for(;*s;++s) { if(*s<'0'||*s>'9'||n>214748364||(n==214748364&&*s>'7')) return false; n=n*10+(*s-'0'); } return true;
}
bool Transfer(Client *c,PlayerBot *b,int inventory_slot,int bot_slot,bool give)
{
 // General slots avoid cursor queues, nested bags, and equipment side effects.
 if(c->GetAggroCount() || b->IsEngaged() || inventory_slot<22 || inventory_slot>29 || bot_slot<0 || bot_slot>EQ::invslot::EQUIPMENT_END || (give&&bot_slot<EQ::invslot::EQUIPMENT_BEGIN)) return false;
 auto engines=database.QueryDatabase("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('character_inventory','takp_bot_inventory','takp_bot_data') AND ENGINE='InnoDB'");
 if(!engines.Success()||engines.RowCount()!=1||atoi(engines.begin()[0])!=3) return false;
 const auto *held=c->GetInv().GetItem(inventory_slot);
 if((give && (!held || !held->IsClassCommon() || (held->IsStackable()&&bot_slot!=EQ::invslot::slotAmmo) || !held->GetCustomDataString().empty() ||
  !held->IsEquipable(b->GetRace(),b->GetClass()) || !held->IsEquipable(bot_slot) || held->GetItem()->ReqLevel>b->GetLevel())) || (!give && held)) return false;
 if(!database.QueryDatabase("START TRANSACTION").Success()) return false;
 auto abort=[&](){database.QueryDatabase("ROLLBACK");return false;};
 auto lock=database.QueryDatabase(fmt::format("SELECT id FROM takp_bot_data WHERE id={} AND owner_character_id={} FOR UPDATE",b->ID(),c->CharacterID()));
 if(!lock.Success() || lock.RowCount()!=1) return abort();
 auto char_item=database.QueryDatabase(fmt::format("SELECT itemid,charges,custom_data FROM character_inventory WHERE id={} AND slotid={} FOR UPDATE",c->CharacterID(),inventory_slot));
 auto bot_item=database.QueryDatabase(fmt::format("SELECT item_id,charges FROM takp_bot_inventory WHERE bot_id={} AND slot={} FOR UPDATE",b->ID(),bot_slot));
 if(!char_item.Success() || !bot_item.Success()) return abort();
 std::unique_ptr<EQ::ItemInstance> returned;
 if(give) {
  if(char_item.RowCount()!=1 || bot_item.RowCount()!=0) return abort();
  auto r=char_item.begin();
  if(strtoul(r[0],nullptr,10)!=held->GetID() || atoi(r[1])!=(held->GetCharges()<0?32767:held->GetCharges()) || (r[2]&&*r[2])) return abort();
  auto worn=database.QueryDatabase(fmt::format("SELECT slot,item_id FROM takp_bot_inventory WHERE bot_id={} FOR UPDATE",b->ID()));
  if(!worn.Success())return abort();
  for(auto w=worn.begin();w!=worn.end();++w) {
   const auto *item=database.GetItem(strtoul(w[1],nullptr,10));if(!item)return abort();
   if(held->GetItem()->Lore[0]=='*'&&item->ID==held->GetID())return abort();
   if(bot_slot==EQ::invslot::slotPrimary&&held->GetItem()->IsType2HWeapon()&&atoi(w[0])==EQ::invslot::slotSecondary)return abort();
   if(bot_slot==EQ::invslot::slotSecondary&&atoi(w[0])==EQ::invslot::slotPrimary&&item->IsType2HWeapon())return abort();
  }
  if(bot_slot==EQ::invslot::slotSecondary&&held->IsWeapon()&&!b->GetSkill(EQ::skills::SkillDualWield))return abort();
  auto add=database.QueryDatabase(fmt::format("INSERT INTO takp_bot_inventory(bot_id,slot,item_id,charges) SELECT {},{},itemid,charges FROM character_inventory WHERE id={} AND slotid={}",b->ID(),bot_slot,c->CharacterID(),inventory_slot));
  if(!add.Success()||add.RowsAffected()!=1) return abort();
  if(!database.QueryDatabase(fmt::format("DELETE FROM character_inventory WHERE id={} AND slotid={}",c->CharacterID(),inventory_slot)).Success()) return abort();
 } else {
  if(char_item.RowCount()!=0 || bot_item.RowCount()!=1) return abort();
  auto r=bot_item.begin(); const auto *item=database.GetItem(strtoul(r[0],nullptr,10));
  if(!item || c->CheckLoreConflict(item)) return abort();
  returned.reset(database.CreateItem(item->ID,atoi(r[1])==32767?-1:atoi(r[1])));
  if(!returned) return abort();
  auto add=database.QueryDatabase(fmt::format("INSERT INTO character_inventory(id,slotid,itemid,charges,custom_data) SELECT {},{},item_id,charges,'' FROM takp_bot_inventory WHERE bot_id={} AND slot={}",c->CharacterID(),inventory_slot,b->ID(),bot_slot));
  if(!add.Success()||add.RowsAffected()!=1) return abort();
  if(!database.QueryDatabase(fmt::format("DELETE FROM takp_bot_inventory WHERE bot_id={} AND slot={}",b->ID(),bot_slot)).Success()) return abort();
 }
 if(!database.QueryDatabase("COMMIT").Success()) { database.QueryDatabase("ROLLBACK"); c->Kick("Inventory transaction result uncertain; reconnect before continuing."); return false; }
 if(give) c->DeleteItemInInventory(inventory_slot,0,true,false);
 else { c->GetInv().PutItem(inventory_slot,*returned); c->SendItemPacket(inventory_slot,returned.get(),ItemPacketTrade); }
 b->LoadEquipment(); b->Save(true); return true;
}
#include "player_bot_commands.inc"
#include "player_bot_trade.inc"
#include "player_bot_roster.inc"
}

bool CanTradePlayerBot(Client *c,Mob *m){auto *b=dynamic_cast<PlayerBot*>(m);return b&&b->BelongsTo(c)&&!b->GetDepop()&&!b->IsEngaged()&&!c->GetAggroCount()&&DistanceSquared(c->GetPosition(),b->GetPosition())<=10000;}
bool FinishPlayerBotTrade(Client *c,Mob *m){auto *b=dynamic_cast<PlayerBot*>(m);return b&&TradePlayerBot(c,b);}

bool HandlePlayerBotCommand(Client *c,const Seperator *sep)
{
 auto action=Strings::ToLower(sep->arg[1]);

 if(action=="assist" && Strings::ToLower(sep->arg[2])=="target") {
  const auto mode=Strings::ToLower(sep->arg[4]);
  if(sep->argnum!=4 || (mode!="on" && mode!="off")) {c->Message(Chat::White,"Select your bot, then use #bot assist target PlayerOrBotName on|off.");return true;}
  auto *bot=dynamic_cast<PlayerBot*>(c->GetTarget());
  if(!bot || !bot->BelongsTo(c) || bot->GetDepop()) {c->Message(Chat::White,"Select one of your spawned bots first.");return true;}
  Mob *source=nullptr;
  if(mode=="on") {
   source=entity_list.GetMob(sep->arg[3]);
   if(!bot->ValidAssistSource(source)) {c->Message(Chat::White,"The named player or bot must be alive in this zone and in your bot's group or raid, and cannot be the selected bot itself.");return true;}
   if(bot->AssistCycle(source)) {c->Message(Chat::White,"Cannot create a circular bot assist chain.");return true;}
  }
  if(!bot->SetAssistSource(source))c->Message(Chat::White,"Could not save assist target; previous setting retained.");
  else if(source)c->Message(Chat::White,"%s now assists %s's selected enemy. Saved; overrides normal assist until disabled. Passive, sit, suspend and retreat still take priority.",bot->GetCleanName(),source->GetCleanName());
  else c->Message(Chat::White,"%s: assist target OFF, saved. Normal owner-based assist restored; existing passive/sit/retreat settings remain in effect.",bot->GetCleanName());
  return true;
 }

 if(action=="dps") {
  const auto mode=Strings::ToLower(sep->arg[3]);
  if(Strings::ToLower(sep->arg[2])!="cast" || (mode!="on" && mode!="off") || sep->argnum<3 || sep->argnum>4) {
   c->Message(Chat::White,"Usage: #bot dps cast on|off [Name|all]. Applies to your spawned bots; saved per bot.");return true;
  }
  const auto name=Strings::ToLower(sep->arg[4]);
 unsigned matched=0;
  for(auto *b:Owned(c)) {
   if(!name.empty() && name!="all" && Strings::ToLower(b->GetCleanName())!=name)continue;
   ++matched;
   if(!b->SetBehavior("dpscast",mode=="on")) c->Message(Chat::White,"Unable to save DPS cast setting for %s.",b->GetCleanName());
   else c->Message(Chat::White,"%s: direct-damage casting %s. DoTs, buffs and heals remain allowed.",b->GetCleanName(),mode=="on"?"ON":"OFF");
  }
  if(!matched)c->Message(Chat::White,"No matching spawned bots owned by you.");
  return true;
 }
 if(action=="camp")action="dismiss";
 if(action=="rez")action="resurrect";
 if(KeepPaceCommand(c,action,sep->arg[2],sep->arg[3]))return true;
 if(FormationCommand(c,action,sep->arg[2],sep->arg[3]))return true;
 if(action=="create" || action=="list") return false;
 if(SavedBotGroup(c,action,sep->arg[2]))return true;
 if(action=="help" || action.empty()) {
  c->Message(Chat::White,"#bot dps cast on|off [Name|all]: toggle direct-damage spells for spawned bots; DoTs, buffs and heals remain allowed. Saved per bot.");
  c->Message(Chat::White,"#bot retreat Name|all on|off: stop combat/casting and follow; overrides assist and sit. Saved per bot until off.");
  c->Message(Chat::White,"#bot sit Name|all on|off: sit and stay until released; off restores prior follow/stay mode.");
  c->Message(Chat::White,"#bot keeppace on|off|status; #bot formation on|off|status; #bot formation gather on|off; #bot formation line on|off");
  c->Message(Chat::White,"#bot create Name Class Race Gender | list | spawn Name | attack/follow/stay/passive/assist/dismiss Name (or all)");
  c->Message(Chat::White,"#bot assist target PlayerOrBotName on|off: select your bot first; saved override for group/raid assisting.");
  c->Message(Chat::White,"#bot backattack Name|all on|off: saved rear combat formation, center then alternating left/right.");
  c->Message(Chat::White,"#bot tankclose Name|all on|off: saved close melee positioning with model clearance.");
  c->Message(Chat::White,"#bot equip/unequip Name InventorySlot(22-29) BotSlot(0-21) | inventory Name | revive Name. Equipment changes require no combat.");
  c->Message(Chat::White,"#bot pet/cure/resurrect/mez/charm/fear/lull/invisibility/levitate/waterbreathing Name (uses your target)");
  c->Message(Chat::White,"#bot supply/withdraw Name InventorySlot ItemID(for withdraw) | supplies Name | ranged/taunt/petenabled Name 0/1 | songs Name SpellID... (up to 4; empty clears)");
  c->Message(Chat::White,"#bot report/summon/petdismiss Name | delete Name confirm (dismiss and remove all equipment/supplies first)");
  c->Message(Chat::White,"#bot abilities Name | discipline/aa Name ID | departures Name | depart/cast Name SpellID | evac Name");
  c->Message(Chat::White,"#bot botgroupcreate/load/delete PartyName | botgrouplist | appearance Name face/hair/haircolor/beard/beardcolor/eye1/eye2 Value");
  c->Message(Chat::White,"#bot spells Name | spellsettingsadd Name SpellID Enabled MinHP MaxHP Priority | blockedbuffs Name SpellID 0/1 | clickitem Name Slot");
  c->Message(Chat::White,"#bot healrotationcreate/start/stop/delete/list Leader | healrotationaddmember/removemember Leader Member | healrotationaddtarget/removetarget Leader Target | healrotationchangeinterval Leader Milliseconds");
  return true;
 }
 if(std::string(sep->arg[2])=="byname") {std::string command="#bot "+action;for(int i=3;i<=sep->argnum;++i)command+=" "+std::string(sep->arg[i]);Seperator normalized(command.c_str());return HandlePlayerBotCommand(c,&normalized);}
 std::string name=sep->arg[2];
 if(name=="spawned"||name=="ownergroup")name="all";
 if(name.empty()||name=="target") {auto *target=dynamic_cast<PlayerBot*>(c->GetTarget());if(target&&target->BelongsTo(c))name=target->GetCleanName();}
 if(name.empty() || name.size()>15 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos) {
  c->Message(Chat::White,"Specify an owned bot name, or all for group commands. See #bot help."); return true;
 }
 auto owned=database.QueryDatabase(fmt::format("SELECT id FROM takp_bot_data WHERE owner_character_id={} AND name='{}'",c->CharacterID(),name));
 const uint32 id=owned.Success()&&owned.RowCount()==1?strtoul(owned.begin()[0],nullptr,10):0;
 if(action.compare(0,12,"healrotation")==0){RotationCommand(c,id,action,sep->arg[3]);return true;}
 if(action=="delete") {DeleteBot(c,id,sep->arg[3]);return true;}
 if(action=="supplies") {
  if(!id){c->Message(Chat::White,"You do not own that bot.");return true;}
  auto rows=database.QueryDatabase(fmt::format("SELECT item_id,quantity FROM takp_bot_supplies WHERE bot_id={} AND quantity>0",id));
  if(rows.Success())for(auto r=rows.begin();r!=rows.end();++r)c->Message(Chat::White,"Item %s: %s",r[0],r[1]);return true;
 }
 if(action=="spawn"&&name=="all"){if(Owned(c).size()>=c->ChallengeBotLimit()){c->Message(Chat::Yellow,"Bot limit reached: %u at your current level. Challenge unlocks: 5, 15, 25, 35, 45.",c->ChallengeBotLimit());return true;}auto roster=database.QueryDatabase(fmt::format("SELECT id FROM takp_bot_data WHERE owner_character_id={} ORDER BY id",c->CharacterID()));if(roster.Success())for(auto r=roster.begin();r!=roster.end()&&Owned(c).size()<c->ChallengeBotLimit();++r)Spawn(c,strtoul(r[0],nullptr,10));return true;}
 if(action=="spawn") { if(!id||!Spawn(c,id)) c->Message(Chat::White,"Unable to spawn that bot."); return true; }
 if(action=="revive") {
  if(!id) {c->Message(Chat::White,"You do not own that bot.");return true;}
  for(auto *b:Owned(c)) if(b->ID()==id) {c->Message(Chat::White,"That bot is already spawned.");return true;}
  if(c->IsDead()) {c->Message(Chat::White,"You must be alive to revive a bot.");return true;}
  bool combat=c->GetAggroCount()!=0;
  for(auto *b:Owned(c)) if(b->IsEngaged() || (b->GetPet() && b->GetPet()->IsEngaged())) combat=true;
  if(c->GetPet() && c->GetPet()->IsEngaged()) combat=true;
  if(combat) {c->Message(Chat::White,"Cannot revive while you, your bots, or your pets are in combat. Clear enemy aggro and try again; there is no revival timer.");return true;}
  // Atomic fallen -> ready transition. Never use saved_at as a death timer:
  // failed spawn attempts and other runtime updates also change that timestamp.
  auto r=database.QueryDatabase(fmt::format("UPDATE takp_bot_runtime SET hp=-1,mana=-1,active=0,generation=generation+1 WHERE bot_id={} AND hp=0",id));
  if(!r.Success()) c->Message(Chat::White,"Could not save revival. Please try again.");
  else if(!r.RowsAffected()) c->Message(Chat::White,"That bot is not fallen. Use #bot spawn %s to summon it.",name.c_str());
  else c->Message(Chat::White,"%s revived. Use #bot spawn %s to summon it with full health and mana.",name.c_str(),name.c_str());
  return true;
 }
 if(action=="inventory") {
  if(!id) {c->Message(Chat::White,"You do not own that bot.");return true;}
  auto rows=database.QueryDatabase(fmt::format("SELECT slot,item_id,charges FROM takp_bot_inventory WHERE bot_id={} ORDER BY slot",id));
  if(!rows.Success()) {c->Message(Chat::White,"Unable to read this bot's equipment. Please try again.");return true;}
  if(!rows.RowCount()) c->Message(Chat::White,"This bot has no equipped items.");
  for(auto r=rows.begin();r!=rows.end();++r) {
   const auto* item=database.GetItem(strtoul(r[1],nullptr,10));
   c->Message(Chat::White,"Slot %s: %s (ID %s, %s charges)",r[0],item?item->Name:"Unknown item",r[1],r[2]);
  }
  return true;
 }
 if(action=="backattack" && (sep->argnum!=3 || (Strings::ToLower(sep->arg[3])!="on" && Strings::ToLower(sep->arg[3])!="off"))) {c->Message(Chat::White,"Use #bot backattack Name|all on|off.");return true;}
 if(action=="tankclose" && (sep->argnum!=3 || (Strings::ToLower(sep->arg[3])!="on" && Strings::ToLower(sep->arg[3])!="off"))) {c->Message(Chat::White,"Use #bot tankclose Name on|off or #bot tankclose all on|off.");return true;}
 unsigned matched=0;
 if(action=="retreat" && (sep->argnum!=3 || (Strings::ToLower(sep->arg[3])!="on" && Strings::ToLower(sep->arg[3])!="off"))) {c->Message(Chat::White,"Use #bot retreat Name on|off or #bot retreat all on|off.");return true;}
 if(action=="sit" && (sep->argnum!=3 || (Strings::ToLower(sep->arg[3])!="on" && Strings::ToLower(sep->arg[3])!="off"))) {c->Message(Chat::White,"Use #bot sit Name on|off or #bot sit all on|off.");return true;}
 for(auto *b:Owned(c)) {
  if(name!="all" && b->ID()!=id) continue;
  ++matched;
  if(action=="retreat") {
   const bool enabled=Strings::ToLower(sep->arg[3])=="on";
   if(!b->SetBehavior("retreat",enabled)) c->Message(Chat::White,"Could not save retreat for %s; its setting was not changed.",b->GetCleanName());
   else c->Message(Chat::White,"%s: retreat %s, saved. %s",b->GetCleanName(),enabled?"ON":"OFF",enabled?"Following without attacks or spells; enemies still retain their hate.":"Normal behavior restored.");
  }
  else if(b->Retreating() && (action=="attack"||action=="assist"||action=="stay"||action=="passive"||action=="sit"||action=="summon"||action=="discipline"||action=="aa"||action=="suspend"||action=="release")) c->Message(Chat::White,"%s is retreating. Use #bot retreat %s off first.",b->GetCleanName(),b->GetCleanName());
  else if(action=="backattack" && (sep->argnum!=3 || (Strings::ToLower(sep->arg[3])!="on" && Strings::ToLower(sep->arg[3])!="off"))) {c->Message(Chat::White,"Use #bot backattack Name|all on|off.");return true;}
 if(action=="tankclose") {
   const bool enabled=Strings::ToLower(sep->arg[3])=="on";
   if(!b->SetBehavior("tankclose",enabled)) c->Message(Chat::White,"Could not save tankclose for %s.",b->GetCleanName());
   else c->Message(Chat::White,"%s: close tank positioning %s, saved. Ranged mode must be off for close positioning.",b->GetCleanName(),enabled?"ON":"OFF");
  }
  else if(action=="sit") {b->ForceSit(Strings::ToLower(sep->arg[3])=="on");c->Message(Chat::White,"%s: forced sit %s. Meditate skill %u; current mana regeneration %d per tick.",b->GetCleanName(),b->forced_sit_?"ON":"OFF",b->GetSkill(EQ::skills::SkillMeditate),b->PlayerManaRegen());}
  else if(b->forced_sit_ && (action=="attack"||action=="follow"||action=="stay"||action=="passive"||action=="assist"||action=="summon"||action=="discipline"||action=="aa")) c->Message(Chat::White,"%s is held sitting. Use #bot sit %s off first.",b->GetCleanName(),b->GetCleanName());
  else if(action=="attack") {if(b->Allowed(c->GetTarget())) b->Engage(c->GetTarget()); else c->Message(Chat::White,"Select an attackable NPC within 200 units.");}
  else if(action=="follow") b->Follow();
  else if(action=="stay") b->Stay();
  else if(action=="passive") b->Passive();
  else if(action=="assist") {b->Assist();if(b->AssistSourceID())c->Message(Chat::White,"%s retains its saved assist target. Select it and use #bot assist target Name off to restore owner-based assisting.",b->GetCleanName());}
  else if(action=="dismiss") {if(b->IsEngaged()||c->GetAggroCount()) {c->Message(Chat::White,"Cannot dismiss during combat.");continue;} if(b->Save(false)){b->DismissPet();b->LeaveCompanionGroup();b->Depop();}}
  else if(action=="pet"||action=="cure"||action=="resurrect"||action=="mez"||action=="charm"||action=="fear"||action=="lull"||action=="invisibility"||action=="invisundead"||action=="levitate"||action=="waterbreathing") {
   if(!b->Utility(action,c->GetTarget()?c->GetTarget():c))c->Message(Chat::White,"%s could not cast: check class, level, target, mana, reagents, range and cooldown.",b->GetCleanName());
  }
  else if(action=="report") {
   c->Message(Chat::White,"Backattack: %s.",b->BackAttackEnabled()?"ON":"OFF");
   if(b->AssistSourceID()){auto *source=b->AssistSource();c->Message(Chat::White,"Assist target: %s (saved %s ID %d).",source?source->GetCleanName():"unavailable; waiting",b->AssistSourceID()>0?"player":"bot",std::abs(b->AssistSourceID()));}else c->Message(Chat::White,"Assist target: OFF (normal owner-based assist).");
   c->Message(Chat::White,"%s level %u: HP %d/%d, mana %d/%d; pet: %s",b->GetCleanName(),b->GetLevel(),b->GetHP(),b->GetMaxHP(),b->GetMana(),b->GetMaxMana(),b->GetPet()?b->GetPet()->GetCleanName():"none");
   c->Message(Chat::White,"Retreat: %s (saved until switched off).",b->Retreating()?"ON":"OFF");
   c->Message(Chat::White,"Direct-damage casting: %s (DoTs, buffs and heals remain allowed).",b->DirectDamageEnabled()?"ON":"OFF");
   c->Message(Chat::White,"Taunt %s; skill %u; attempts since spawn %u (attempts can fail).",b->IsTaunting()?"ON":"OFF",b->GetSkill(EQ::skills::SkillTaunt),b->bot_taunt_attempts_);
  }
  else if(action=="spells") b->ListSpells();
  else if(action=="appearance"){int value;if(!SlotNumber(sep->arg[4],value)||!b->SetAppearance(sep->arg[3],value))c->Message(Chat::White,"Invalid appearance for this race/gender. Use face/hair/haircolor/beard/beardcolor/eye1/eye2 and a value.");else c->Message(Chat::White,"Appearance saved; dismiss and spawn the bot to apply it.");}
  else if(action=="abilities")b->ListAbilities();
  else if(action=="departures")b->ListDepartures();
  else if(action=="evac"){if(!b->Evacuate())c->Message(Chat::White,"No available group evacuation spell.");}
  else if(action=="cast"||action=="depart"){int spell;if(!SlotNumber(sep->arg[3],spell)||spell>65535||!IsValidSpell(spell)||(action=="depart"&&!IsEffectInSpell(spell,SE_Teleport)&&!IsEffectInSpell(spell,SE_Succor))||!b->CastKnown(spell,c->GetTarget()?c->GetTarget():c))c->Message(Chat::White,"Cannot cast that spell on this target.");}
  else if(action=="discipline"||action=="aa"){int ability;if(!SlotNumber(sep->arg[3],ability)||!(action=="aa"?b->ActivateAA(ability,c->GetTarget()?c->GetTarget():c):(ability<256&&b->UseDiscipline(ability))))c->Message(Chat::White,"Ability unavailable: check class, level, target, state and cooldown.");}
  else if(action=="spellsettingsadd"||action=="spellsettingsupdate") {
   int spell,enabled,minhp,maxhp,priority;
   if(!SlotNumber(sep->arg[3],spell)||spell>65535||!SlotNumber(sep->arg[4],enabled)||!SlotNumber(sep->arg[5],minhp)||!SlotNumber(sep->arg[6],maxhp)||!SlotNumber(sep->arg[7],priority)||!b->SetSpellSetting(spell,enabled,minhp,maxhp,priority))c->Message(Chat::White,"Invalid spell settings. Use SpellID Enabled(0/1) MinHP MaxHP Priority(0-1000).");
  }
  else if(action=="spellsettingsdelete") {int spell;if(SlotNumber(sep->arg[3],spell)&&database.QueryDatabase(fmt::format("DELETE FROM takp_bot_spell_settings WHERE bot_id={} AND spell_id={}",b->ID(),spell)).Success())b->LoadConfiguration();}
  else if(action=="spellsettings") {auto rows=database.QueryDatabase(fmt::format("SELECT spell_id,enabled,min_hp,max_hp,priority FROM takp_bot_spell_settings WHERE bot_id={}",b->ID()));if(rows.Success())for(auto r=rows.begin();r!=rows.end();++r)c->Message(Chat::White,"Spell %s: enabled %s, HP %s-%s, priority %s",r[0],r[1],r[2],r[3],r[4]);}
  else if(action=="blockedbuffs") {int spell,value;if(!SlotNumber(sep->arg[3],spell)||spell>65535||!SlotNumber(sep->arg[4],value)||value>1||!b->BlockBuff(spell,value))c->Message(Chat::White,"Use blockedbuffs Name SpellID 0/1.");}
  else if(action=="clickitem") {int slot;if(!SlotNumber(sep->arg[3],slot)||!b->ClickItem(slot,c->GetTarget()?c->GetTarget():c))c->Message(Chat::White,"Item click refused: check item, charges, target, level and cooldown.");}
  else if(action=="suspend"||action=="release") b->SetBehavior("suspend",action=="suspend");
  else if(action=="enforcespellsettings"||action=="announcecasts"||action=="healpercent") {int value;if(!SlotNumber(sep->arg[3],value)||!b->SetBehavior(action,value))c->Message(Chat::White,"Setting rejected.");}
  else if(action=="summon") {if(!b->IsEngaged()&&!c->GetAggroCount()){b->GMMove(c->GetX(),c->GetY(),c->GetZ(),c->GetHeading());b->Follow();}}
  else if(action=="petdismiss") {if(!b->IsEngaged()&&!c->GetAggroCount()){b->DismissPet();b->SetOption("petenabled",0);b->Save(true);}}
  else if(action=="ranged"||action=="taunt"||action=="petenabled"||action=="followdistance") {int value;if(!SlotNumber(sep->arg[3],value)||!b->SetOption(action,value))c->Message(Chat::White,"Setting rejected.");else if(action=="taunt")c->Message(Chat::White,"%s: taunt %s; skill %u. Use #bot report %s to check attempts.",b->GetCleanName(),value?"ON":"OFF",b->GetSkill(EQ::skills::SkillTaunt),b->GetCleanName());}
  else if(action=="songs") {std::vector<uint16> ids;bool valid=true;for(int n=3;n<7&&*sep->arg[n];++n){int id;if(!SlotNumber(sep->arg[n],id)||id>65535){valid=false;break;}ids.push_back(id);}if(!valid||!b->SetSongs(ids))c->Message(Chat::White,"Use up to four bard spells at this bot's level.");}
  else if(action=="supply"||action=="withdraw") {int slot,item=0;if(!SlotNumber(sep->arg[3],slot)||(action=="withdraw"&&!SlotNumber(sep->arg[4],item))||!Supply(c,b,slot,item,action=="supply"))c->Message(Chat::White,"Supply transfer refused; use a top-level general slot, plain components and no combat.");}
  else if((action=="equip"||action=="unequip") && name!="all") {
   int from,slot;
   if(!SlotNumber(sep->arg[3],from)||!SlotNumber(sep->arg[4],slot)||!Transfer(c,b,from,slot,action=="equip"))
    c->Message(Chat::White,"Transfer refused. Use an empty destination, compatible equipment without custom data, and no combat; check lore and weapon conflicts.");
   else c->Message(Chat::White,"Equipment transfer saved.");
  } else {c->Message(Chat::White,"Unknown command. See #bot help.");break;}
 }
 if(!matched) c->Message(Chat::White,"No matching spawned bot. Use #bot spawn Name.");
 return true;
}

bool PlayerBotProjects(Mob *mob,unsigned short spell,Mob *target,bool consume){auto *b=dynamic_cast<PlayerBot*>(mob);return b&&b->Projects(spell,target,consume);}

bool PlayerBotInstantDisc(Mob *mob,unsigned short skill,bool consume){auto *b=dynamic_cast<PlayerBot*>(mob);if(!b||!b->InstantDisc(skill))return false;if(consume)b->FadeDiscipline();return true;}

int PlayerBotRegen(Mob *mob,bool mana){auto *b=dynamic_cast<PlayerBot*>(mob);return b?(mana?b->PlayerManaRegen():b->PlayerHPRegen()):0;}

bool PlayerBotRetreating(Mob* mob) {
 if(!mob) return false;
 auto* bot=dynamic_cast<PlayerBot*>(mob);
 if(!bot && mob->IsPet()) bot=dynamic_cast<PlayerBot*>(mob->GetOwner());
 return bot && bot->Retreating();
}

void FollowPlayerBotRetreat(Mob* mob) {
 if(auto* bot=dynamic_cast<PlayerBot*>(mob)) {
  if(auto* owner=bot->Owner()) {bot->Follow();HandlePlayerBotFormation(bot,owner);}
  else bot->StopNavigation();
 } else if(auto* owner=mob->GetOwner()) {
  // The pet follows its retreating bot, without changing its saved pet order.
  if(DistanceSquaredNoZ(mob->GetPosition(),owner->GetPosition())>100.f) {
   auto destination=owner->GetPosition();destination.z=mob->GetFixedZ(destination);
   mob->RunTo(destination.x,destination.y,destination.z);
  } else if(mob->IsMoving()) mob->StopNavigation();
 }
}

bool HandlePlayerBotFormation(Mob* mob, Mob* follow) {
 auto* bot = dynamic_cast<PlayerBot*>(mob);
 if (!bot || !follow || !follow->IsClient() || !bot->BelongsTo(follow->CastToClient())) return false;
 auto* owner=follow->CastToClient();
 bot->follow_motion_.Update(Timer::GetCurrentTime(),owner->GetX(),owner->GetY(),owner->GetDeltaX(),owner->GetDeltaY());
 if(bot->FormationFollow(owner)) {bot->follow_moving_=false;return true;}
 return bot->NormalFollow(owner);
}
int PlayerBotFollowPace(Mob* mob,int maximum) {
 auto* bot=dynamic_cast<PlayerBot*>(mob);
 return bot?bot->FollowPace(maximum):maximum;
}
void NotePlayerBotMovementPacket(Mob* mob) {
 if(auto* bot=dynamic_cast<PlayerBot*>(mob)) ++bot->movement_packets_;
}
void TracePlayerBotPacket(Mob* mob, SpawnPositionUpdate_Struct* packet, bool no_delta) {
#ifndef PLAYER_BOT_DIAGNOSTICS
 return;
#endif
 auto* bot=dynamic_cast<PlayerBot*>(mob);
 if(!bot || Timer::GetCurrentTime()-bot->trace_start_>300000 || bot->trace_packets_>=20000) return;
 auto* owner=bot->Owner();
 if(!owner) return;
 ++bot->trace_packets_;
 // Capture the same representation used to relay real-player movement.
 SpawnPositionUpdate_Struct player{};
 owner->MakeSpawnUpdate(&player);
 auto* file=std::fopen("/tmp/takp-bot-packets.log","a");
 if(!file) return;
 std::fprintf(file,"%llu bot=%u zero=%d moving=%d actual=%.3f,%.3f,%.3f wire=%d,%d,%d delta=%.4f,%.4f,%.4f anim=%d heading=%d speed=%.3f owner=%d,%d,%d odelta=%.4f,%.4f,%.4f oanim=%d oheading=%d\n",
  static_cast<unsigned long long>(Timer::GetCurrentTime()),bot->ID(),no_delta,bot->IsMoving(),bot->GetX(),bot->GetY(),bot->GetZ(),
  packet->x_pos,packet->y_pos,packet->z_pos,packet->delta_yzx.GetX(),packet->delta_yzx.GetY(),packet->delta_yzx.GetZ(),packet->anim_type,packet->heading,bot->GetCurrentSpeed(),
  player.x_pos,player.y_pos,player.z_pos,player.delta_yzx.GetX(),player.delta_yzx.GetY(),player.delta_yzx.GetZ(),player.anim_type,player.heading);
 std::fclose(file);
}
