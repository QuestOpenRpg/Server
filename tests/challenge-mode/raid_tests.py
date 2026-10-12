from pathlib import Path
p=Path(__file__).parent
zone=p.resolve().parents[1]/"zone"
s=(zone/'player_bot_features.inc').read_text();s=s[s.index('bool PlayerBot::JoinRaid'):s.index('void PlayerBot::LeaveRaid')]
fixture=r'''
#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
constexpr unsigned MAX_RAID_GROUPS=12,MAX_GROUP_MEMBERS=6,MAX_RAID_MEMBERS=72;
struct Client;struct Raid;
unsigned inserted_group=999;
struct Result{bool Success(){return true;}};
struct DB{Result QueryDatabase(std::string s){unsigned raid;int bot;assert(sscanf(s.c_str(),"INSERT INTO raid_members(raidid,charid,groupid,_class,level,name) VALUES(%u,%d,%u",&raid,&bot,&inserted_group)==3);return {};}} database;
struct Raid{unsigned count[12]{};bool leader=true,group_leader=true,present=false;unsigned saved_group=0;
 unsigned GetGroup(Client*){return 0;}unsigned GetGroup(const char*){return saved_group;}
 bool IsLeader(Client*){return leader;}bool IsGroupLeader(const char*){return group_leader;}bool IsRaidMember(const char*){return present;}
 unsigned GroupCount(unsigned g){assert(g<12);return count[g];}unsigned RaidCount(){unsigned n=0;for(auto c:count)n+=c;return n;}
 unsigned GetID(){return 1;}void LearnMembers(){}void VerifyRaid(){}void SendRaidAddAll(const char*){}void GroupUpdate(unsigned){}
};
struct Client{Raid *r;bool challenge=true;unsigned level=45;Raid*GetRaid(){return r;}bool IsChallengeMode(){return challenge;}unsigned GetLevel(){return level;}const char*GetName(){return "Owner";}};
struct PlayerBot{unsigned bot_id_=123,raid_id_=0;bool JoinRaid(Client*);const char*GetName(){return "Bot";}unsigned GetClass(){return 1;}unsigned GetLevel(){return 45;}void SetRaidGrouped(bool){};};
struct ServerRaidGeneralAction_Struct{unsigned rid,zoneid;char playername[64];};enum {ServerOP_RaidAdd};
struct ServerPacket{alignas(ServerRaidGeneralAction_Struct) char pBuffer[sizeof(ServerRaidGeneralAction_Struct)]{};ServerPacket(int,unsigned){}};
struct World{void SendPacket(ServerPacket*){}}worldserver;struct Zone{unsigned GetZoneID(){return 1;}}z,*zone=&z;
void strn0cpy(char*d,const char*s,size_t n){strncpy(d,s,n);d[n-1]=0;}
'''
tests=r'''
int main(){Raid r;Client c{&r};PlayerBot b;
r.count[0]=6;assert(b.JoinRaid(&c)&&inserted_group==1);
c.level=44;assert(!b.JoinRaid(&c));c.level=45;
c.challenge=false;assert(!b.JoinRaid(&c));c.challenge=true;
r.leader=false;assert(!b.JoinRaid(&c));r.leader=true;
for(auto &n:r.count)n=6;assert(!b.JoinRaid(&c));
r.count[7]=5;assert(b.JoinRaid(&c)&&inserted_group==7);
r.present=true;r.saved_group=7;r.count[7]=6;assert(b.JoinRaid(&c)&&inserted_group==7);
puts("PASS: actual raid join fills available subgroups only for unlocked Challenge leaders, preserves restored bot groups, rejects full raids and unauthorized overflow");}
'''
(p/'raid-test.cpp').write_text(fixture+s+tests,newline='\n')
