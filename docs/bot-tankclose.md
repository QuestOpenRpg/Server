# Close tank positioning

`#bot tankclose Name on` enables closer melee positioning for a spawned owned bot. `#bot tankclose Name off` restores ordinary positioning. `all` can replace the name. The setting is saved per bot and defaults to off. No database migration is required; it uses `takp_bot_options`.

While within melee range, the bot approaches a point on its current side of the enemy, one unit beyond the sum of their server bounding radii. If overlapping, it steps outward. A settling tolerance avoids constant corrections. Invalid ground, unsafe routes and clearance beyond melee reach fall back to ordinary combat movement. The adjustment yields while casting, rooted, stunned, mezzed, feared, blind, sitting, suspended, passive, retreating or in ranged mode.

This does not change enemy hate selection, taunt, attack reach or follow distance. Enable taunt separately with `#bot taunt Name 1`. Only enable close positioning on bots intended to stand nearer the enemy; enabling it on all melee bots will not give one tank priority.

Server bounding radii approximate model footprints. Visual clearance for unusually shaped models and movement during combat require in-game verification. Short positioning adjustments complete before the normal melee AI resumes its attack processing.
