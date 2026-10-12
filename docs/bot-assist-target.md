# Assist a named player or bot

Select one of your spawned bots, then enter:

```
#bot assist target Drax on
```

Only the selected owned bot is changed. Drax must be a living player or bot in the same zone and the selected bot's group or raid (including another raid subgroup). Selecting an attackable enemy is sufficient; the named player does not need autoattack on. The bot uses its usual range and target eligibility checks and follows target changes on its assist tick, every 500 ms.

While enabled, this replaces owner-based automatic assisting and restricts hostile actions, including the bot's pet, to the designated source's selected enemy. Normal `#bot assist Name` can enable the bot's active stance but cannot clear this override. Passive, forced sit, suspend and retreat still take priority. Friendly healing and buffs retain their existing behavior.

Select the affected bot again to disable:

```
#bot assist target Drax off
```

Off clears the selected bot's override even if the named source is no longer present. Existing passive/sit/retreat settings remain in effect. `#bot report Name` displays assist status.

The setting persists per bot through respawn and zoning using permanent player/bot IDs. If the source is missing, dead, disconnected or no longer grouped/raided with the bot, the bot waits for a valid source/target rather than falling back to its owner. Self-assist and circular bot assist chains are rejected. Clearing the bot's target/hate during switching does not erase enemies' hate toward the bot.
