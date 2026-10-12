# Rear combat formation

```
#bot backattack Name on
#bot backattack Name off
#bot backattack all on
#bot backattack all off
```

Changes spawned bots owned by the issuing player. The setting saves per bot and defaults to off. Use `#bot report Name` to check it. Leave it off for the tank.

Eligible enabled bots attacking the same enemy share rear slots: center, left, right, farther left, farther right. Slots use ascending permanent bot IDs. Members of the same group or raid share the layout, including different owners. Separate enemies receive separate layouts. The formation follows enemy heading, uses model-aware clearance, and approaches along short arc segments rather than a line through the enemy. Normal melee attacks resume when the bot settles into position.

Backattack takes priority over tankclose; disabling it restores any existing tankclose setting. It does not replace idle follow formations or change the selected enemy/assist source. Ranged mode, casting, roots, stuns, mez, fear, blindness, passive mode, suspend, forced sit and retreat prevent repositioning. A bot being targeted by the enemy stops trying to get behind it.

Invalid terrain, water, unsafe routes, or a layout too large to remain in melee reach fall back to ordinary combat positioning. Server size estimates are approximate: visually verify clearance in game, especially for unusual models, moving enemies and crowded terrain. Layout geometry tested for one through five bots; larger raid crowds may not fit within melee reach.
