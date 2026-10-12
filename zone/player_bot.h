#pragma once
class Client;
class Mob;
class Seperator;
bool IsPlayerBot(Mob *mob);
bool PlayerBotRetreating(Mob *mob);
void FollowPlayerBotRetreat(Mob *mob);
int PlayerBotDoTDamage(Mob *mob,unsigned short spell_id,int value);
bool PlayerBotSpellTargetAllowed(Mob *mob,Mob *target,unsigned short spell_id);
bool PlayerBotDirectDamageAllowed(Mob *mob,unsigned short spell_id);
bool IsPlayerBotPet(Mob *mob);
bool ConsumePlayerBotReagents(Mob *mob,unsigned short spell_id);
void ApplyPlayerBotDamageMultiplier(Mob *mob,int offense,int &damage,int skill);
bool HandlePlayerBotCommand(Client *owner, const Seperator *sep);
void RestorePlayerBots(Client *owner);
void SavePlayerBots(Client *owner);

unsigned char PlayerBotDiscipline(Mob *mob);
bool CanTradePlayerBot(Client *owner,Mob *bot);
bool FinishPlayerBotTrade(Client *owner,Mob *bot);

bool PlayerBotMassBuff(Mob *mob,unsigned short spell);

bool PlayerBotProjects(Mob *mob,unsigned short spell,Mob *target,bool consume=false);

bool PlayerBotInstantDisc(Mob *mob,unsigned short skill,bool consume=false);

int PlayerBotRegen(Mob *mob,bool mana);

bool HandlePlayerBotFormation(Mob* mob, Mob* follow);
int PlayerBotFollowPace(Mob* mob,int maximum);
void NotePlayerBotMovementPacket(Mob* mob);
struct SpawnPositionUpdate_Struct;
void TracePlayerBotPacket(Mob* mob, SpawnPositionUpdate_Struct* packet, bool no_delta);

void EnforceChallengeBotLimit(Client *owner);

bool HandlePlayerBotTankClose(Mob *mob);

bool HandlePlayerBotBackAttack(Mob *mob);
