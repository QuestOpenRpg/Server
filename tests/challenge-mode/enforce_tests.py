from pathlib import Path
p=Path(__file__).parent
zone=p.resolve().parents[1]/"zone";s=(zone/'player_bot.cpp').read_text();s=s[s.index('void EnforceChallengeBotLimit('):s.index('void RestorePlayerBots(')]
fixture=r'''
#include "challenge_mode.h"
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
namespace Chat {enum{Red,Yellow};}
struct Client{bool enabled=true,raid=false;unsigned level=5,id=1;unsigned ChallengeBotLimit(){return ChallengeMode::BotLimit(enabled,level,raid);}template<class...T>void Message(T...){};};
struct PlayerBot{unsigned id,owner;bool depop=false,saved=false,pet_dismissed=false,left=false;
 unsigned ID(){return id;}bool Save(bool active){assert(!active);saved=true;return true;}const char*GetCleanName(){return "Testbot";}void DismissPet(){pet_dismissed=true;}void LeaveCompanionGroup(){left=true;}void Depop(){depop=true;}};
std::vector<PlayerBot*> all;
std::vector<PlayerBot*> Owned(Client*c){std::vector<PlayerBot*>r;for(auto*b:all)if(b->owner==c->id&&!b->depop)r.push_back(b);return r;}
'''
tests=r'''
int main(){Client c;std::vector<PlayerBot> bots;for(unsigned i=0;i<8;i++)bots.push_back({8-i,i==7?2u:1u});for(auto&b:bots)all.push_back(&b);
 c.raid=true;c.level=45;EnforceChallengeBotLimit(&c);assert(Owned(&c).size()==7);
 c.level=44;EnforceChallengeBotLimit(&c);assert(Owned(&c).size()==4);
 for(auto&b:bots)if(b.depop)assert(b.saved&&b.pet_dismissed&&b.left);
 c.level=4;EnforceChallengeBotLimit(&c);assert(Owned(&c).empty());assert(!bots[7].depop);
 for(auto&b:bots){b.depop=false;b.saved=false;b.left=false;b.pet_dismissed=false;}
 c.level=45;c.enabled=false;EnforceChallengeBotLimit(&c);assert(Owned(&c).size()==5);
 puts("PASS: actual enforcement dismisses excess bots/pets after delevel or override-off, saves inactive state, and never touches another owner's bot");}
'''
(p/'enforce-test.cpp').write_text(fixture+s+tests,newline='\n')
