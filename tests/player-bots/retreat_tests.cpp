// Real production methods and AI branch, with world/DB/navigation test doubles.
// This exercises retreat decisions; it does not simulate the game client or pathfinder.
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <iostream>
enum {eaSitting,eaStanding};
struct Timer {static int GetCurrentTime(){return 100;}};
struct RestGate {int resets=0;void Reset(int){++resets;}};
namespace fmt {template<class... T> std::string format(const char*,T...){return "query";}}
struct Result {bool good;bool Success(){return good;}};
struct Database {bool good=true;Result QueryDatabase(std::string){return {good};}} database;
bool fear_pathing=true;
#define RuleB(category,rule) fear_pathing
struct Mob {
 virtual ~Mob()=default;
 bool casting=false, engaged=false, rooted=false, stunned=false, mezzed=false, feared=false;
 bool currently_fleeing=false, moving=false, pet_flag=false;
 int hate=0,bardsong=0,appearance=eaStanding,normal_ai_calls=0,follow_calls=0,fear_calls=0;
 int interrupts=0,stops=0,end_combat=0;
 Mob* target=nullptr;Mob* owner=nullptr;Mob* pet=nullptr;
 bool IsPet(){return pet_flag;} Mob* GetOwner(){return owner;}
 Mob* GetPet(){return pet;} bool IsCasting(){return casting;}
 bool IsMezzed(){return mezzed;} bool IsStunned(){return stunned;}
 bool IsRooted(){return rooted;} bool IsFeared(){return feared;}
 int GetAppearance(){return appearance;} void SetAppearance(int a){appearance=a;}
 void InterruptSpell(){casting=false;++interrupts;}
 void WipeHateList(){hate=0;} void SetTarget(Mob* p){target=p;}
 void StopNavigation(){moving=false;++stops;}
 void AI_Event_NoLongerEngaged(){++end_combat;}
 void DoFearMovement(){++fear_calls;}
 void AI_Process();
};
struct PlayerBot:Mob {
 std::map<std::string,int> options_;
 bool forced_sit_=false,following=false;
 bool backattack_moving_=false,tank_close_moving_=false;
 int backattack_next_=0,tank_close_next_=0;
 int rest_started_=0,bot_id_=1,casting_spell_id=1,stance=1;
 RestGate rest_gate_;
 void ForceSit(bool v){forced_sit_=v;}
 void ResetFormationMotion(){}
 void Follow(){following=true;}
 void Stay(){following=false;}
 void Assist(){stance=1;}
 void Passive(){stance=0;}
 bool DirectDamageAllowed(int){return false;}
 bool SetBehavior(const std::string&,int);
 #include "retreat_members.inc"
};
void FollowPlayerBotRetreat(Mob* m){++m->follow_calls;}
#include "retreat_definitions.inc"
int main(){
 PlayerBot bot;Mob enemy,pet,other;
 assert(!bot.Retreating());assert(!PlayerBotRetreating(nullptr));
 bot.pet=&pet;pet.owner=&bot;pet.pet_flag=true;
 bot.forced_sit_=true;bot.appearance=eaSitting;bot.casting=true;bot.bardsong=55;
 bot.target=&enemy;bot.hate=25;bot.engaged=true;bot.moving=true;
 pet.casting=true;pet.target=&enemy;pet.hate=20;pet.engaged=true;
 enemy.hate=999;enemy.target=&bot;
 database.good=false;
 assert(!bot.SetBehavior("retreat",1));assert(!bot.Retreating());assert(bot.casting);
 database.good=true;
 assert(!bot.SetBehavior("retreat",2));assert(!bot.Retreating());
 assert(bot.SetBehavior("retreat",1));assert(bot.Retreating());
 assert(bot.following && !bot.forced_sit_ && bot.appearance==eaStanding);
 assert(!bot.casting && bot.bardsong==0 && bot.hate==0 && !bot.target);
 assert(!pet.casting && pet.hate==0 && !pet.target);
 assert(PlayerBotRetreating(&pet));assert(!PlayerBotRetreating(&other));
 assert(enemy.hate==999 && enemy.target==&bot);
 bot.AI_Process();pet.AI_Process();
 assert(!bot.engaged && bot.follow_calls==1 && bot.normal_ai_calls==0);
 assert(!pet.engaged && pet.follow_calls==1 && pet.normal_ai_calls==0);
 // Being hit again must not restart combat or continually fire combat-end events.
 bot.hate=100;bot.target=&enemy;bot.AI_Process();
 assert(bot.hate==0 && !bot.target && bot.follow_calls==2 && bot.end_combat==1);
 bot.rooted=true;bot.moving=true;bot.AI_Process();assert(!bot.moving && bot.follow_calls==2);
 bot.rooted=false;bot.stunned=true;bot.AI_Process();assert(bot.follow_calls==2);
 bot.stunned=false;bot.mezzed=true;bot.AI_Process();assert(bot.follow_calls==2);
 bot.mezzed=false;bot.feared=true;bot.currently_fleeing=true;bot.AI_Process();
 assert(bot.fear_calls==1 && bot.follow_calls==2);
 fear_pathing=false;bot.AI_Process();assert(bot.fear_calls==1 && bot.follow_calls==2);
 bot.feared=false;bot.AI_Process();assert(bot.follow_calls==3);
 assert(bot.SetBehavior("retreat",0));assert(!bot.Retreating());assert(!PlayerBotRetreating(&pet));
 bot.AI_Process();pet.AI_Process();assert(bot.normal_ai_calls==1 && pet.normal_ai_calls==1);
 assert(bot.stance==1); // Retreat must not overwrite the prior assist/passive preference.
 bot.stance=0;bot.options_["suspend"]=1;
 assert(bot.SetBehavior("retreat",1));assert(bot.following && bot.stance==0);
 assert(bot.SetBehavior("retreat",0));assert(!bot.following && bot.stance==0);
 // A different bot remains entirely unaffected.
 PlayerBot second;second.AI_Process();assert(second.normal_ai_calls==1 && !second.Retreating());
 std::cout<<"Retreat persistence failure, combat cancellation, pet, repeat-hit, control-effect and resume checks passed\n";
}
