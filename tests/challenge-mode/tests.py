from pathlib import Path
p=Path(__file__).parent
zone=p.resolve().parents[1]/"zone"
impl=(zone/'challenge_mode.inc').read_text().replace('#include "../common/seperator.h"','')
exp=(zone/'exp.cpp').read_text();start=exp.index('void Client::GetExpLoss(');end=exp.index('// This will return a multiplier',start)
exp=exp[start:end]
fixture=r'''
#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include "challenge_mode.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
using uint8=uint8_t;using uint16=uint16_t;using uint32=uint32_t;
namespace Chat {enum {White,Red,Yellow};}
namespace Strings {std::string ToLower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return std::tolower(c);});return s;}}
struct Seperator {int argnum=0;std::string arg[11];Seperator(std::initializer_list<std::string> v){int i=0;for(auto &s:v)arg[i++]=s;argnum=i-1;}};
struct Row{std::string s;const char *operator[](int)const{return s.c_str();}};
struct Result {bool ok;std::vector<Row> rows;bool Success(){return ok;}size_t RowCount(){return rows.size();}Row begin(){return rows.empty()?Row{"0"}:rows[0];}};
struct DB {bool fail=false;std::map<unsigned,int> state;Result QueryDatabase(std::string s){if(fail)return {false,{}};unsigned id=0;int value=0;if(s.find("SELECT")==0){sscanf(s.c_str(),"SELECT enabled FROM takp_character_challenge WHERE character_id=%u",&id);return {true,state.count(id)?std::vector<Row>{{std::to_string(state[id])}}:std::vector<Row>{}};}assert(sscanf(s.c_str(),"INSERT INTO takp_character_challenge(character_id,enabled) VALUES(%u,%d)",&id,&value)==2);state[id]=value;return {true,{}};}} database;
struct Zone {unsigned id=1;unsigned GetZoneID(){return id;}} zone_value,*zone=&zone_value;
bool rules_LeaveCorpses=true;int rules_DeathItemLossLevel=10,rules_DeathExpLossLevel=10,rules_DeathExpLossMaxLevel=255;float rules_EXPLossMultiplier=1;
#define RuleB(c,n) rules_##n
#define RuleI(c,n) rules_##n
#define RuleR(c,n) rules_##n
enum {Killed_DUEL=1,Killed_PVP=2,Killed_Self=3};constexpr uint16 SPELL_UNKNOWN=65535;
struct Mob{bool player=false;Mob* owner=nullptr;bool IsClient(){return player;}Mob* GetOwner(){return owner;}};
struct Client {unsigned id=1;uint8 level=1;bool raid=false,challenge_mode_enabled_=false;int enforcement=0;
 struct Bind{unsigned zoneId=1;};struct PP{Bind binds[5];} pp;
 struct Buff{uint16 spellid;bool client;} buffs[1]{};
 std::vector<std::string> messages;
 bool IsChallengeMode()const{return challenge_mode_enabled_;}bool LoadChallengeMode();bool SetChallengeMode(bool);unsigned ChallengeBotLimit();bool LeavesEquipmentCorpse()const;
 unsigned CharacterID(){return id;}uint8 GetLevel()const{return level;}bool HasRaid(){return raid;}PP& GetPP(){return pp;}
 bool IsBecomeNPC(){return false;}unsigned GetMaxTotalSlots(){return 0;}unsigned GetEXPForLevel(unsigned l){assert(l>=1);return (l-1)*(l-1)*(l-1)*1000;}
 void GetExpLoss(Mob*,uint16,int&,uint8);
 void Message(int,const char *f,...){char b[1024];va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);messages.emplace_back(b);}
};
void EnforceChallengeBotLimit(Client*c){c->enforcement++;}
'''
tests=r'''
void cmd(Client &c,std::initializer_list<std::string> args){Seperator s(args);command_challenge(&c,&s);}
int main(){
 for(unsigned l=1;l<=65;l++){
  assert(ChallengeMode::BotLimit(false,l,true)==5);
  assert(ChallengeMode::BotLimit(true,l,false)==(l<5?0:l<15?1:l<25?2:l<35?3:l<45?4:5));
  assert(ChallengeMode::BotLimit(true,l,true)==(l>=45?71:ChallengeMode::BotLimit(true,l,false)));
  assert(ChallengeMode::BlockBook(true,l,true)==(l<45));
  assert(!ChallengeMode::BlockBook(false,l,true)&&!ChallengeMode::BlockBook(true,l,false));
 }
 Client c;assert(c.LoadChallengeMode()&&!c.IsChallengeMode());
 c.level=2;cmd(c,{"#challenge","mode","on"});assert(!c.IsChallengeMode());
 c.level=1;zone->id=202;cmd(c,{"#challenge","mode","on"});assert(!c.IsChallengeMode());zone->id=1;
 for(auto &b:c.pp.binds){b.zoneId=202;cmd(c,{"#challenge","mode","on"});assert(!c.IsChallengeMode());b.zoneId=1;}
 cmd(c,{"#challenge","mode","on"});assert(c.IsChallengeMode()&&database.state[1]==1&&c.enforcement==1);
 cmd(c,{"#challenge","mode","off"});assert(c.IsChallengeMode());
 Client reconnected;assert(reconnected.LoadChallengeMode()&&reconnected.IsChallengeMode());
 Client other;other.id=2;assert(other.LoadChallengeMode()&&!other.IsChallengeMode());
 cmd(c,{"#challenge","mode","override","off"});assert(!c.IsChallengeMode()&&database.state[1]==0);
 c.level=65;zone->id=202;c.pp.binds[0].zoneId=202;
 cmd(c,{"#challenge","mode","override","on"});assert(c.IsChallengeMode());
 cmd(c,{"#challenge","mode","override","off","extra"});assert(c.IsChallengeMode());
 database.fail=true;cmd(c,{"#challenge","mode","override","off"});assert(c.IsChallengeMode());assert(!c.LoadChallengeMode());database.fail=false;
 c.messages.clear();cmd(c,{"#challenge"});assert(c.messages[0].find("override")==std::string::npos);
 for(int level=1;level<12;level++){
  c.level=level;c.challenge_mode_enabled_=true;assert(c.LeavesEquipmentCorpse());
  int loss=0;c.GetExpLoss(nullptr,SPELL_UNKNOWN,loss,0);assert(loss>0);
  c.GetExpLoss(nullptr,SPELL_UNKNOWN,loss,Killed_DUEL);assert(loss==0);
  c.challenge_mode_enabled_=false;assert(c.LeavesEquipmentCorpse()==(level>=10));
  c.GetExpLoss(nullptr,SPELL_UNKNOWN,loss,0);assert((loss>0)==(level>=10));
 }
 puts("PASS: unlock boundaries, raid capacity, book gates, activation/bind checks, normal off denied, unrestricted self override, persistence/isolation/failure handling, corpse policy and actual XP-loss method");
}
'''
(p/'test.cpp').write_text(fixture+impl+'\n'+exp+tests,newline='\n')
print('Generated tests exercising actual command/persistence and XP-loss implementation')
