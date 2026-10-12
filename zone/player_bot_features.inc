// Internal implementation included by player_bot.cpp, inside its anonymous namespace.
bool PlayerBot::Friendly(Mob *target)
{
 if(!target || !Owner()) return false;
 if(target==this || target==Owner() || target==GetPet()) return true;
 for(auto *member:Friends()) if(member && (member==target || member->GetPet()==target)) return true;
 return false;
}

std::vector<Mob*> PlayerBot::Friends()
{
 std::vector<Mob*> result;
 if(auto *g=GetGroup())for(auto *m:g->members)if(m)result.push_back(m);
 if(auto *r=GetRaid())for(auto &entry:r->members)if(entry.GroupNumber==r->GetGroup(GetName())&&*entry.membername) {
  auto *m=entity_list.GetMob(entry.membername);if(m&&(m->IsClient()||IsPlayerBot(m)))result.push_back(m);
 }
 return result;
}

bool PlayerBot::JoinRaid(Client *owner)
{
 auto *raid=owner->GetRaid();if(!raid||bot_id_>2147483647)return false;
 auto group=raid->GetGroup(owner);if(group>=MAX_RAID_GROUPS)return false;
 if(!raid->IsLeader(owner)&&!raid->IsGroupLeader(owner->GetName()))return false;
 const bool present=raid->IsRaidMember(GetName());
 if(present)group=raid->GetGroup(GetName());
 if(group>=MAX_RAID_GROUPS)return false;
 if(raid->GroupCount(group)-(present?1:0)>=MAX_GROUP_MEMBERS && owner->IsChallengeMode() && owner->GetLevel()>=45 && raid->IsLeader(owner)) {
  for(unsigned g=0;g<MAX_RAID_GROUPS;++g)if(raid->GroupCount(g)<MAX_GROUP_MEMBERS){group=g;break;}
 }
 if(raid->GroupCount(group)-(present?1:0)>=MAX_GROUP_MEMBERS||(!present&&raid->RaidCount()>=MAX_RAID_MEMBERS))return false;
 // Negative IDs are reserved for bots; player character IDs are positive.
 auto inserted=database.QueryDatabase(fmt::format("INSERT INTO raid_members(raidid,charid,groupid,_class,level,name) VALUES({},{},{},{},{},'{}') ON DUPLICATE KEY UPDATE raidid=VALUES(raidid),groupid=VALUES(groupid),level=VALUES(level),name=VALUES(name)",raid->GetID(),-static_cast<int>(bot_id_),group,GetClass(),GetLevel(),GetName()));
 if(!inserted.Success())return false;
 raid_id_=raid->GetID();SetRaidGrouped(true);raid->LearnMembers();raid->VerifyRaid();raid->SendRaidAddAll(GetName());raid->GroupUpdate(group);
 ServerPacket packet(ServerOP_RaidAdd,sizeof(ServerRaidGeneralAction_Struct));auto *a=reinterpret_cast<ServerRaidGeneralAction_Struct*>(packet.pBuffer);a->rid=raid_id_;a->zoneid=zone->GetZoneID();strn0cpy(a->playername,GetName(),64);worldserver.SendPacket(&packet);
 return true;
}

void PlayerBot::LeaveRaid()
{
 auto *raid=GetRaid();raid_id_=0;SetRaidGrouped(false);if(!raid)return;
 auto lease=database.QueryDatabase(fmt::format("SELECT generation FROM takp_bot_runtime WHERE bot_id={}",bot_id_));
 if(!lease.Success()||lease.RowCount()!=1||strtoul(lease.begin()[0],nullptr,10)!=generation_)return;
 const auto group=raid->GetGroup(GetName());raid->RemoveMember(GetName());raid->GroupUpdate(group);
}

bool PlayerBot::Components(uint16 id,bool consume)
{
 if(!IsValidSpell(id)) return false;
 std::map<int,int> needed;
 const auto &s=spells[id];
 for(int n=0;n<4;++n) if(s.components[n]>0 && s.component_counts[n]>0) needed[s.components[n]]+=s.component_counts[n];
 for(const auto &entry:needed) {
  auto rows=database.QueryDatabase(fmt::format("SELECT quantity FROM takp_bot_supplies WHERE bot_id={} AND item_id={}{}",bot_id_,entry.first,consume?" FOR UPDATE":""));
  if(!rows.Success()||rows.RowCount()!=1||strtoul(rows.begin()[0],nullptr,10)<entry.second) return false;
 }
 // Nonconsumed focus components follow the server's item definitions. Instrument
 // codes (1..4) are not item IDs; bard instrument bonuses use worn equipment.
 for(int n=0;n<4;++n) if(s.NoexpendReagent[n]>4 && !IsSummonPetSpell(id)) {
  const int item=s.NoexpendReagent[n]; bool worn=false;
  for(auto equipped:equipment) if(equipped==item) worn=true;
  if(!worn) {
   auto rows=database.QueryDatabase(fmt::format("SELECT quantity FROM takp_bot_supplies WHERE bot_id={} AND item_id={} AND quantity>0",bot_id_,item));
   if(!rows.Success()||!rows.RowCount()) return false;
  }
 }
 if(consume) for(const auto &entry:needed) {
  auto result=database.QueryDatabase(fmt::format("UPDATE takp_bot_supplies SET quantity=quantity-{} WHERE bot_id={} AND item_id={} AND quantity>={}",entry.second,bot_id_,entry.first,entry.second));
  if(!result.Success()||result.RowsAffected()!=1) return false;
 }
 return true;
}

bool PlayerBot::CastSpell(uint16 id,uint16 target_id,EQ::spells::CastingSlot slot,int32 casttime,int32 mana_cost,uint32 *finish,uint32 item_slot,uint32 timer,uint32 duration,uint32 type,int16 *resist)
{
 if(Retreating()||forced_sit_||!IsValidSpell(id)||IsCasting()||!SpellPermitted(id,entity_list.GetMob(target_id))) return false;
 pending_click_slot_=-1;pending_click_spell_=0;
 if(item_slot>=EQ::invslot::EQUIPMENT_BEGIN&&item_slot<=EQ::invslot::EQUIPMENT_END) {pending_click_slot_=item_slot;pending_click_spell_=id;}
 else if(!Components(id,false))return false;
 if(item_slot==0xFFFFFFFF){if(casttime>=0)casttime=GetActSpellCasttime(id,casttime);if(mana_cost>=0)mana_cost=GetActSpellCost(id,mana_cost);}
 if((mana_cost<0?GetActSpellCost(id,spells[id].mana):mana_cost)>GetMana())return false;
 if(GetAppearance()==eaSitting)SetAppearance(eaStanding);rest_started_=0;
 const bool started=NPC::CastSpell(id,target_id,slot,casttime,mana_cost,finish,item_slot,timer,duration,type,resist);
 if(started && (id==SPELL_HARM_TOUCH || id==SPELL_LAY_ON_HANDS))
  utility_recasts_[id]=Timer::GetCurrentTime()+1000u*(id==SPELL_HARM_TOUCH?HarmTouchReuseTime:LayOnHandsReuseTime);
 return started;
}

bool PlayerBot::ConsumeComponents(uint16 id)
{
 if(!IsValidSpell(id))return false;
 if(pending_click_slot_>=0&&pending_click_spell_==id) {
  const auto slot=pending_click_slot_;pending_click_slot_=-1;pending_click_spell_=0;
  const auto *item=database.GetItem(EquippedItem(slot));if(!item||item->Click.Effect!=id)return false;if(item->MaxCharges<=0)return true;
  auto r=database.QueryDatabase(fmt::format("SELECT charges FROM takp_bot_inventory WHERE bot_id={} AND slot={}",bot_id_,slot));
  if(!r.Success()||r.RowCount()!=1)return false;
  int charges=atoi(r.begin()[0]);if(charges==32767)return true;if(charges<=0)return false;
  auto used=database.QueryDatabase(fmt::format("UPDATE takp_bot_inventory SET charges=charges-1 WHERE bot_id={} AND slot={} AND charges={}",bot_id_,slot,charges));
  return used.Success()&&used.RowsAffected()==1;
 }
 bool uses=false; for(int n=0;n<4;++n) if(spells[id].components[n]>0||spells[id].NoexpendReagent[n]>4) uses=true;
 if(!uses) return true;
 if(!database.QueryDatabase("START TRANSACTION").Success()) return false;
 if(!Components(id,true)) {database.QueryDatabase("ROLLBACK");return false;}
 if(!database.QueryDatabase("COMMIT").Success()) {
  database.QueryDatabase("ROLLBACK"); InterruptSpell(); if(Owner()) Owner()->Message(Chat::Red,"Bot reagent transaction failed; casting stopped."); return false;
 }
 return true;
}

bool PlayerBot::TrySpell(uint16 id,Mob *target)
{
 if(!Knows(id)||!target||IsCasting()||GetActSpellCost(id,spells[id].mana)>GetMana()||utility_recasts_[id]>Timer::GetCurrentTime()) return false;
 const auto &s=spells[id];
 if(s.targettype==ST_Self && !Projects(id,target)) target=this;
 if(s.targettype==ST_Group || s.targettype==ST_GroupTeleport) target=this;
 if(Projects(id,target)&&DistanceSquared(GetPosition(),target->GetPosition())>10000)return false;
 if(IsDetrimentalSpell(id) && (!stance_||!Allowed(target))) return false;
 if(s.targettype!=ST_Self && s.targettype!=ST_Group && s.targettype!=ST_GroupTeleport && DistanceSquared(GetPosition(),target->GetPosition())>GetSpellRange(id,s.range)*GetSpellRange(id,s.range)) return false;
 if(!CheckLosFN(target)||!Components(id,false)) return false;
 if(GetClass()==Class::Bard) { InterruptSpell(); bardsong=0; }
 // Item slot avoids NPC AI's 8-bit index for utility spells. Cooldowns are saved
 // independently and charged at cast start, including interrupted attempts.
 if(!CastSpell(id,target->GetID(),EQ::spells::CastingSlot::Item,s.cast_time,s.mana)) return false;
 utility_recasts_[id]=Timer::GetCurrentTime()+std::max<int>(s.recast_time,s.cast_time+1500);
 return true;
}

bool PlayerBot::Cure(Mob *target)
{
 if(!Friendly(target)||target->HasDied()) return false;
 bool poison=false,disease=false,curse=false;
 for(int n=0;n<target->GetMaxTotalSlots();++n) {
  auto &b=target->GetBuffs()[n]; if(!IsValidSpell(b.spellid)||b.counters<=0) continue;
  poison|=IsEffectInSpell(b.spellid,SE_PoisonCounter);
  disease|=IsEffectInSpell(b.spellid,SE_DiseaseCounter);
  curse|=IsEffectInSpell(b.spellid,SE_CurseCounter);
 }
 if(!poison&&!disease&&!curse) return false;
 for(auto it=utility_spells_.rbegin();it!=utility_spells_.rend();++it) {
  if(!IsCureSpell(*it)) continue;
  const auto &s=spells[*it]; bool match=false;
  for(int n=0;n<EFFECT_COUNT;++n) if(s.base[n]<0 && ((poison&&s.effectid[n]==SE_PoisonCounter)||(disease&&s.effectid[n]==SE_DiseaseCounter)||(curse&&s.effectid[n]==SE_CurseCounter))) match=true;
  if(match&&TrySpell(*it,target)) return true;
 }
 return false;
}

bool PlayerBot::Utility(const std::string &kind,Mob *target)
{
 if(kind=="cure") return Cure(target);
 if(kind=="resurrect") {
  if(!target||!target->IsCorpse()||!target->CastToCorpse()->IsPlayerCorpse()||!target->CastToCorpse()->IsRezzable()||IsEngaged()||!Owner()||Owner()->GetAggroCount()) return false;
 } else if(kind=="pet") {if(GetPet()||IsEngaged())return false;target=this;}
 else if(kind=="mez"||kind=="charm"||kind=="fear"||kind=="lull") {if(!Allowed(target))return false;}
 else if(!Friendly(target)) return false;
 for(auto it=utility_spells_.rbegin();it!=utility_spells_.rend();++it) {
  auto id=*it;
  bool match=(kind=="pet"&&IsSummonPetSpell(id)) || (kind=="resurrect"&&IsEffectInSpell(id,SE_Revive)) ||
   (kind=="mez"&&IsMezSpell(id)) || (kind=="charm"&&IsCharmSpell(id)&&!GetPet()) ||
   (kind=="fear"&&IsFearSpell(id)) || (kind=="lull"&&(IsEffectInSpell(id,SE_Lull)||IsHarmonySpell(id))) ||
   (kind=="invisibility"&&IsEffectInSpell(id,SE_Invisibility)) ||
   (kind=="invisundead"&&IsEffectInSpell(id,SE_InvisVsUndead)) ||
   (kind=="levitate"&&IsEffectInSpell(id,SE_Levitate)) ||
   (kind=="waterbreathing"&&IsEffectInSpell(id,SE_WaterBreathing));
  if(match&&TrySpell(id,target)) return true;
 }
 return false;
}

bool PlayerBot::ControlAdds()
{
 if(GetClass()!=Class::Enchanter && GetClass()!=Class::Bard) return false;
 auto *owner=Owner(); if(!owner) return false;
 for(const auto &entry:entity_list.GetNPCList()) {
  auto *target=entry.second;
  if(target==owner->GetTarget()||target==GetTarget()||!Allowed(target)||target->GetHPRatio()<99||!target->IsEngaged()) continue;
  bool attacking_friend=Friendly(target->GetTarget());
  if(attacking_friend && Utility("mez",target)) return true;
 }
 return false;
}

bool PlayerBot::LoadFeatures()
{
 if(!database.QueryDatabase(fmt::format("INSERT IGNORE INTO takp_bot_settings(bot_id) VALUES({})",bot_id_)).Success()) return false;
 auto rows=database.QueryDatabase(fmt::format("SELECT ranged_mode,taunt_enabled,pet_enabled,follow_distance FROM takp_bot_settings WHERE bot_id={}",bot_id_));
 if(!rows.Success()||rows.RowCount()!=1)return false;
 auto r=rows.begin(); ranged_mode_=atoi(r[0]);SetTaunting(atoi(r[1]));pet_enabled_=atoi(r[2]);SetFollowDistance(strtoul(r[3],nullptr,10));
 auto songs=database.QueryDatabase(fmt::format("SELECT spell_id FROM takp_bot_songs WHERE bot_id={} ORDER BY position LIMIT 4",bot_id_));
 if(!songs.Success())return false;
 for(auto s=songs.begin();s!=songs.end();++s) if(GetClass()==Class::Bard&&Knows(atoi(s[0]))) songs_.push_back(atoi(s[0]));
 return LoadConfiguration();
}

bool PlayerBot::SetOption(const std::string &option,int value)
{
 std::string column;
 if(option=="ranged") column="ranged_mode";
 else if(option=="taunt") column="taunt_enabled";
 else if(option=="petenabled") column="pet_enabled";
 else if(option=="followdistance") column="follow_distance";
 else return false;
 if(value<0 || value>(option=="followdistance"?2500:1))return false;
 if(!database.QueryDatabase(fmt::format("UPDATE takp_bot_settings SET {}={} WHERE bot_id={}",column,value,bot_id_)).Success())return false;
 if(option=="ranged")ranged_mode_=value;
 if(option=="taunt")SetTaunting(value);
 if(option=="petenabled")pet_enabled_=value;
 if(option=="followdistance") {SetFollowDistance(value);ResetFormationMotion();}
 return true;
}

bool PlayerBot::SetSongs(const std::vector<uint16> &ids)
{
 if(GetClass()!=Class::Bard||ids.size()>4)return false;
 for(auto id:ids)if(!Knows(id))return false;
 if(!database.QueryDatabase("START TRANSACTION").Success())return false;
 auto fail=[](){database.QueryDatabase("ROLLBACK");return false;};
 if(!database.QueryDatabase(fmt::format("DELETE FROM takp_bot_songs WHERE bot_id={}",bot_id_)).Success())return fail();
 for(size_t n=0;n<ids.size();++n)if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_songs VALUES({},{},{})",bot_id_,n,ids[n])).Success())return fail();
 if(!database.QueryDatabase("COMMIT").Success())return fail();
 songs_=ids; next_song_=0; InterruptSpell();bardsong=0;return true;
}

bool PlayerBot::SavePetState()
{
 for(auto table:{"takp_bot_pets","takp_bot_pet_buffs","takp_bot_pet_items"})
  if(!database.QueryDatabase(fmt::format("DELETE FROM {} WHERE bot_id={}",table,bot_id_)).Success())return false;
 auto *pet=GetPet(); if(!pet||!pet->IsNPC()||pet->IsCharmedPet()||pet->HasDied()||GetHP()<=0)return true;
 auto id=pet->CastToNPC()->GetPetSpellID(); if(!IsValidSpell(id))return true;
 if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_pets VALUES({},{},{},{},'{}')",bot_id_,id,pet->GetHP(),pet->GetMana(),Strings::Escape(pet->GetName()))).Success())return false;
 for(int n=0;n<pet->GetMaxTotalSlots();++n) {
  const auto &b=pet->GetBuffs()[n];if(!IsValidSpell(b.spellid))continue;
  if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_pet_buffs VALUES({},{},{},{},{},{},{},{},{})",bot_id_,n,b.spellid,b.casterlevel,b.ticsremaining,b.counters,b.melee_rune,b.magic_rune,b.instrumentmod)).Success())return false;
 }
 std::vector<SpellBuff_Struct> buffs(pet->CastToNPC()->GetPetMaxTotalSlots());uint32 items[EQ::invslot::EQUIPMENT_COUNT]{};char name[64]{};
 pet->CastToNPC()->GetPetState(buffs.data(),items,name);
 for(int n=0;n<EQ::invslot::EQUIPMENT_COUNT;++n)if(items[n])
  if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_pet_items VALUES({},{},{})",bot_id_,n,items[n])).Success())return false;
 return true;
}

bool PlayerBot::RestorePet()
{
 auto rows=database.QueryDatabase(fmt::format("SELECT spell_id,hp,mana,name FROM takp_bot_pets WHERE bot_id={}",bot_id_));
 if(!rows.Success())return false;if(!rows.RowCount())return true;
 auto r=rows.begin();const int id=atoi(r[0]);if(!Knows(id)||!IsSummonPetSpell(id)||atoi(r[1])<=0)return true;
 MakePet(id,spells[id].teleport_zone,r[3]);auto *pet=GetPet();if(!pet||!pet->IsNPC())return false;
 auto saved=database.QueryDatabase(fmt::format("SELECT slot,spell_id,caster_level,ticks,counters,melee_rune,magic_rune,instrument_mod FROM takp_bot_pet_buffs WHERE bot_id={}",bot_id_));
 auto items=database.QueryDatabase(fmt::format("SELECT slot,item_id FROM takp_bot_pet_items WHERE bot_id={}",bot_id_));
 if(!saved.Success()||!items.Success())return false;
 std::vector<SpellBuff_Struct> state(pet->CastToNPC()->GetPetMaxTotalSlots());for(auto &b:state)b.spellid=SPELL_UNKNOWN;
 uint32 equipment[EQ::invslot::EQUIPMENT_COUNT]{};
 for(auto s=saved.begin();s!=saved.end();++s){int n=atoi(s[0]);if(n<0||n>=state.size()||!IsValidSpell(atoi(s[1])))continue;auto &b=state[n];b.spellid=atoi(s[1]);b.level=atoi(s[2]);b.duration=atoi(s[3]);b.counters=atoi(s[4]);b.bard_modifier=atoi(s[7]);}
 for(auto s=items.begin();s!=items.end();++s){int n=atoi(s[0]);if(n>=0&&n<EQ::invslot::EQUIPMENT_COUNT)equipment[n]=strtoul(s[1],nullptr,10);}
 pet->CastToNPC()->SetPetState(state.data(),equipment);pet->CastToNPC()->CalcBonuses();pet->SetHP(std::min<int>(atoi(r[1]),pet->GetMaxHP()));pet->SetMana(std::min<int>(atoi(r[2]),pet->GetMaxMana()));return true;
}

void PlayerBot::RestoreBuffEffects()
{
 rooted=FindType(SE_Root);Silence(FindType(SE_Silence));Amnesia(FindType(SE_Amnesia));
 invulnerable=FindType(SE_DivineAura);invisible_undead=FindType(SE_InvisVsUndead);invisible_animals=FindType(SE_InvisVsAnimals);see_invis=FindType(SE_SeeInvis)?1:0;
 if(FindType(SE_Invisibility))SetInvisible(INVIS_NORMAL,false);
 if(FindType(SE_Levitate))SetFlyMode(GravityBehavior::Levitating);
 for(int n=0;n<GetMaxTotalSlots();++n)if(IsValidSpell(buffs[n].spellid)) {
  auto id=buffs[n].spellid;
  for(int e=0;e<EFFECT_COUNT;++e) {
   if(spells[id].effectid[e]==SE_WeaponProc)AddProcToWeapon(GetProcID(id,e),100+spells[id].base2[e],id);
   if(spells[id].effectid[e]==SE_Illusion&&spells[id].base[e]>0)SendIllusionPacket(spells[id].base[e]);
  }
 }
}

bool PlayerBot::CanRangedAttack(Mob *target)
{
 if(!ranged_mode_||!stance_||!Allowed(target)||!CheckLosFN(target))return false;
 const auto *bow=database.GetItem(equipment[EQ::invslot::slotRange]);const auto *ammo=database.GetItem(ammo_item_);
 if(!bow||bow->ItemType!=EQ::item::ItemTypeBow||!ammo||ammo->ItemType!=EQ::item::ItemTypeArrow||!GetSkill(EQ::skills::SkillArchery))return false;
 const float distance=DistanceSquared(GetPosition(),target->GetPosition());const int range=bow->Range+ammo->Range;
 if(distance<25||distance>range*range)return false;
 auto charges=database.QueryDatabase(fmt::format("SELECT charges FROM takp_bot_inventory WHERE bot_id={} AND slot={}",bot_id_,EQ::invslot::slotAmmo));
 return charges.Success()&&charges.RowCount()==1&&atoi(charges.begin()[0])>0;
}

void PlayerBot::RangedAttack(Mob *target)
{
 if(!ranged_mode_||!stance_||!Allowed(target)||IsCasting()||IsStunned()||IsMezzed()||IsFeared()||DivineAura()||!CheckLosFN(target)||!ranged_bot_timer_.Check())return;
 const auto *bow=database.GetItem(equipment[EQ::invslot::slotRange]);const auto *ammo=database.GetItem(ammo_item_);
 if(!bow||bow->ItemType!=EQ::item::ItemTypeBow||!ammo||ammo->ItemType!=EQ::item::ItemTypeArrow||!GetSkill(EQ::skills::SkillArchery))return;
 const float distance=DistanceSquared(GetPosition(),target->GetPosition());const int range=bow->Range+ammo->Range;
 if(distance<25 || distance>range*range)return;
 auto used=database.QueryDatabase(fmt::format("UPDATE takp_bot_inventory SET charges=charges-1 WHERE bot_id={} AND slot={} AND charges>0",bot_id_,EQ::invslot::slotAmmo));
 if(!used.Success()||used.RowsAffected()!=1)return;
 auto empty=database.QueryDatabase(fmt::format("DELETE FROM takp_bot_inventory WHERE bot_id={} AND slot={} AND charges=0",bot_id_,EQ::invslot::slotAmmo));
 if(empty.Success()&&empty.RowsAffected()==1)ammo_item_=0;
 ranged_bot_timer_.Start(std::max<int>(500,bow->Delay*100));
 int damage=1;target->AvoidDamage(this,damage,true);if(damage>0&&!target->AvoidanceCheck(this,EQ::skills::SkillArchery))damage=DMG_MISS;
 if(damage>0)damage=target->IsImmuneToMelee(this)?DMG_INVUL:CalcMeleeDamage(target,bow->Damage+ammo->Damage,EQ::skills::SkillArchery);
 ProjectileAnimation(target,ammo->ID,true,-1,-1,-1,-1,EQ::skills::SkillArchery);
 auto *beacon=new Beacon(this,3000);entity_list.AddBeacon(beacon);beacon->Projectile(this,target,EQ::skills::SkillArchery,damage,bow->Damage+ammo->Damage);CommonBreakInvisible();
}
bool PlayerBot::CastKnown(uint16 id,Mob *target)
{
 if(!Knows(id)||!target)return false;
 if(IsEffectInSpell(id,SE_Teleport)||IsEffectInSpell(id,SE_Succor)) {
  if(spells[id].targettype!=ST_GroupTeleport&&spells[id].targettype!=ST_Group)return false;
  target=this;
 } else if(IsEffectInSpell(id,SE_Revive)) {
  if(!target->IsCorpse()||!target->CastToCorpse()->IsPlayerCorpse()||!target->CastToCorpse()->IsRezzable()||IsEngaged()||Owner()->GetAggroCount())return false;
 } else if(IsBeneficialSpell(id)&&!Friendly(target))return false;
 if(IsSummonPetSpell(id)){if(GetPet()||IsEngaged())return false;target=this;}
 if(spells[id].targettype==ST_Pet)target=GetPet();
 return target&&TrySpell(id,target);
}

void PlayerBot::ListDepartures()
{
 if(!Owner())return;
 for(int id=1;id<SPDAT_RECORDS;++id)if(Knows(id)&&(spells[id].targettype==ST_GroupTeleport||spells[id].targettype==ST_Group)&&(IsEffectInSpell(id,SE_Teleport)||IsEffectInSpell(id,SE_Succor)))Owner()->Message(Chat::White,"%u: %s -> %s",id,spells[id].name,spells[id].teleport_zone);
}

bool PlayerBot::Evacuate()
{
 for(int id=SPDAT_RECORDS-1;id>0;--id)if(Knows(id)&&IsEffectInSpell(id,SE_Succor)&&(spells[id].targettype==ST_GroupTeleport||spells[id].targettype==ST_Group)&&CastKnown(id,this))return true;
 return false;
}
