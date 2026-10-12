bool PlayerBot::LoadConfiguration()
{
 auto settings=database.QueryDatabase(fmt::format("SELECT spell_id,enabled,min_hp,max_hp,priority FROM takp_bot_spell_settings WHERE bot_id={}",bot_id_));
 auto options=database.QueryDatabase(fmt::format("SELECT setting,value FROM takp_bot_options WHERE bot_id={}",bot_id_));
 auto blocked=database.QueryDatabase(fmt::format("SELECT spell_id FROM takp_bot_blocked_buffs WHERE bot_id={}",bot_id_));
 if(!settings.Success()||!options.Success()||!blocked.Success())return false;
 spell_settings_.clear();options_.clear();blocked_buffs_.clear();
 for(auto r=settings.begin();r!=settings.end();++r)spell_settings_[atoi(r[0])]={atoi(r[1]),atoi(r[2]),atoi(r[3]),atoi(r[4])};
 for(auto r=options.begin();r!=options.end();++r)options_[r[0]]=atoi(r[1]);
 for(auto r=blocked.begin();r!=blocked.end();++r)blocked_buffs_.insert(atoi(r[0]));
 std::stable_sort(AIspells.begin(),AIspells.end(),[&](const AISpells_Struct &a,const AISpells_Struct &b){
  auto x=spell_settings_.find(a.spellid),y=spell_settings_.find(b.spellid);return (x==spell_settings_.end()?0:x->second.priority)<(y==spell_settings_.end()?0:y->second.priority);
 });
 if(options_["suspend"]){Passive();Stay();}
 if(Retreating()) BeginRetreat();
 return true;
}

bool PlayerBot::SetSpellSetting(uint16 id,int enabled,int min_hp,int max_hp,int priority)
{
 if(!Knows(id)||enabled<0||enabled>1||min_hp<0||min_hp>max_hp||max_hp>100||priority<-1000||priority>1000)return false;
 if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_spell_settings VALUES({},{},{},{},{},{}) ON DUPLICATE KEY UPDATE enabled=VALUES(enabled),min_hp=VALUES(min_hp),max_hp=VALUES(max_hp),priority=VALUES(priority)",bot_id_,id,enabled,min_hp,max_hp,priority)).Success())return false;
 return LoadConfiguration();
}

bool PlayerBot::SetBehavior(const std::string &key,int value)
{
 const std::set<std::string> booleans={"suspend","enforcespellsettings","announcecasts","dpscast","retreat","tankclose","backattack"};
 const std::set<std::string> percentages={"healpercent"};
 if((booleans.count(key)&&(value<0||value>1))||(percentages.count(key)&&(value<0||value>100))||(!booleans.count(key)&&!percentages.count(key)))return false;
 if(!database.QueryDatabase(fmt::format("INSERT INTO takp_bot_options VALUES({},'{}',{}) ON DUPLICATE KEY UPDATE value=VALUES(value)",bot_id_,key,value)).Success())return false;
 options_[key]=value;
 if(key=="backattack") {if(backattack_moving_ || tank_close_moving_)StopNavigation();backattack_moving_=false;tank_close_moving_=false;backattack_next_=0;}
 if(key=="tankclose") {if(tank_close_moving_) StopNavigation();tank_close_moving_=false;tank_close_next_=0;}
 if(key=="retreat") {
  if(value) BeginRetreat();
  else {WipeHateList();SetTarget(nullptr);ResetFormationMotion();if(options_["suspend"]) Stay();else Follow();}
 }
 if(key=="dpscast" && !value && IsCasting() && !DirectDamageAllowed(casting_spell_id)) InterruptSpell();
 if(key=="suspend"){if(value){Passive();Stay();}else{Assist();Follow();}}
 return true;
}

bool PlayerBot::BlockBuff(uint16 id,bool blocked)
{
 if(!IsValidSpell(id)||!IsBeneficialSpell(id))return false;
 auto q=blocked?fmt::format("INSERT IGNORE INTO takp_bot_blocked_buffs VALUES({},{})",bot_id_,id):fmt::format("DELETE FROM takp_bot_blocked_buffs WHERE bot_id={} AND spell_id={}",bot_id_,id);
 if(!database.QueryDatabase(q).Success())return false;
 if(blocked){blocked_buffs_.insert(id);BuffFadeBySpellID(id);}else blocked_buffs_.erase(id);return true;
}

bool PlayerBot::SpellPermitted(uint16 id,Mob *target)
{
 if(Retreating()||!DirectDamageAllowed(id)||options_["suspend"]||utility_recasts_[id]>Timer::GetCurrentTime())return false;
 auto setting=spell_settings_.find(id);
 if(setting==spell_settings_.end()) {if(options_["enforcespellsettings"])return false;}
 else if(!setting->second.enabled || (target&&(target->GetHPRatio()<setting->second.min_hp||target->GetHPRatio()>setting->second.max_hp)))return false;
 if(options_["announcecasts"]&&Owner())Owner()->Message(Chat::White,"%s begins %s.",GetCleanName(),spells[id].name);
 return true;
}

void PlayerBot::ListSpells()
{
 if(!Owner())return;
 for(int id=1;id<SPDAT_RECORDS;++id)if(Knows(id))Owner()->Message(Chat::White,"%u: %s (level %u, mana %d)",id,spells[id].name,spells[id].classes[GetClass()-1],spells[id].mana);
}

bool PlayerBot::ClickItem(int slot,Mob *target)
{
 if(slot<EQ::invslot::EQUIPMENT_BEGIN||slot>EQ::invslot::EQUIPMENT_END||!target)return false;
 const auto *item=database.GetItem(EquippedItem(slot));if(!item||!IsValidSpell(item->Click.Effect)||item->Click.Level>GetLevel())return false;
 const auto id=item->Click.Effect;
 if(IsBeneficialSpell(id)?!Friendly(target):!Allowed(target))return false;
 if(utility_recasts_[id]>Timer::GetCurrentTime()||IsCasting())return false;
 if(item->MaxCharges>0){auto r=database.QueryDatabase(fmt::format("SELECT charges FROM takp_bot_inventory WHERE bot_id={} AND slot={}",bot_id_,slot));if(!r.Success()||!r.RowCount()||atoi(r.begin()[0])<=0)return false;}
 if(!CastSpell(id,target->GetID(),EQ::spells::CastingSlot::Item,item->CastTime,0,nullptr,slot))return false;
 utility_recasts_[id]=Timer::GetCurrentTime()+std::max<int>(spells[id].recast_time,item->CastTime+1500);return true;
}

bool PlayerBot::RotationHeal(bool &configured)
{
 configured=false;
 auto rows=database.QueryDatabase(fmt::format("SELECT r.leader_bot_id,r.interval_ms,r.next_member,(r.next_cast_at<=CURRENT_TIMESTAMP(3)) FROM takp_bot_heal_rotations r JOIN takp_bot_heal_rotation_members m ON m.leader_bot_id=r.leader_bot_id WHERE m.bot_id={} AND r.owner_character_id={} AND r.enabled=1",bot_id_,owner_character_id_));
 if(!rows.Success()||!rows.RowCount())return false;configured=true;
 auto r=rows.begin();if(!atoi(r[3]))return false;const auto leader=strtoul(r[0],nullptr,10);const int interval=atoi(r[1]);const auto next=strtoul(r[2],nullptr,10);
 auto member_rows=database.QueryDatabase(fmt::format("SELECT bot_id FROM takp_bot_heal_rotation_members WHERE leader_bot_id={} ORDER BY bot_id",leader));
 if(!member_rows.Success())return false;std::vector<uint32> members;
 for(auto m=member_rows.begin();m!=member_rows.end();++m)for(auto *bot:Owned(Owner()))if(bot->ID()==strtoul(m[0],nullptr,10)&&Friendly(bot)&&!bot->HasDied()&&!bot->options_["suspend"]&&!bot->IsStunned()&&!bot->IsMezzed())members.push_back(bot->ID());
 if(members.empty())return false;auto turn=std::find(members.begin(),members.end(),next);if(turn==members.end())turn=members.begin();if(*turn!=bot_id_)return false;
 auto targets=database.QueryDatabase(fmt::format("SELECT name FROM takp_bot_heal_rotation_targets WHERE leader_bot_id={}",leader));if(!targets.Success())return false;
 Mob *hurt=nullptr;
 for(auto t=targets.begin();t!=targets.end();++t){auto *m=entity_list.GetMob(t[0]);if(Friendly(m)&&!m->HasDied()&&m->GetHPRatio()<85&&(!hurt||m->GetHPRatio()<hurt->GetHPRatio()))hurt=m;}
 if(!targets.RowCount())for(auto *m:Friends())if(m&&!m->HasDied()&&m->GetHPRatio()<85&&(!hurt||m->GetHPRatio()<hurt->GetHPRatio()))hurt=m;
 if(!hurt)return false;
 auto following=turn+1;if(following==members.end())following=members.begin();
 auto claim=database.QueryDatabase(fmt::format("UPDATE takp_bot_heal_rotations SET next_member={},next_cast_at=TIMESTAMPADD(MICROSECOND,{},CURRENT_TIMESTAMP(3)) WHERE leader_bot_id={} AND next_member={} AND next_cast_at<=CURRENT_TIMESTAMP(3) AND enabled=1",*following,interval*1000,leader,next));
 return claim.Success()&&claim.RowsAffected()==1&&HealTarget(hurt);
}
