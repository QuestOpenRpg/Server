# Validation and review notes

Baseline: `bd838e594a29cd8d9b9bd43cd862c8692f01a10e` (EQMacEmu).

The bot gameplay code was compiled and deployed on Debian 12 x86_64 in an
EQMacEmu Docker installation. Gameplay feedback drove the included fixes for
movement, sitting, armor appearance, taunt, Harmony and casting controls.
The most recent player-rule audit still requires broad gameplay confirmation.
This branch is not a claim that every spell, AA, skill, race or zone is verified.

## Tests

Run `python3 tests/player-bots/run.py` with a C++20 compiler available. Set `CXX`
if necessary. Tests do not launch a game server or modify a database.

All fourteen included standalone programs passed during preparation:
formation layout, formation modes, resting, forced-sit eligibility, movement
prediction, continuity, spacing, velocity, fine pacing, DPS spell policy,
AE targeting and beneficial permissions, crowd-control limits, backstab and
hate selection. Movement tests exercise the production headers. The targeting,
spell and skill fixtures contain extracted production snippets with controlled
stub entities, not complete zone integration tests; keep these extracts aligned
with future production changes. Spell-policy fixtures use actual server records.

## Source packaging

Source was collected from the deployed development checkout. Unrelated Docker SQL
relocations, database dumps, account data and local configuration are excluded.
Eleven additive database migrations are included in numeric order. The prototype
GM companion command is retained because it is referenced by the existing command
registration; the player-facing feature is `#bot`.

Two packaging changes distinguish this checkout from the deployed build:
normal CMake output location and opt-in bounded movement diagnostics. The full
packaged checkout has not yet been independently rebuilt as a release binary.
No precompiled installer is included. The older 1.0.1 binary package does not
contain all changes on this branch.

## Gameplay checks before a binary release

Retreat update (2026-10-06): the deployed-source zone target compiled, and all
15 standalone regression programs passed. Retreat tests extract the actual
production state-transition methods and AI branch and run them against world,
database and navigation test doubles. They cover failed persistence, forced-sit
release, cast cancellation, pets, repeated incoming hate, control effects and
restoring normal behavior. They do not prove client-visible movement or real
combat behavior; gameplay verification is still required.

- Engage enemies, enable retreat for one bot and then all, attack a new target,
  and verify they keep following without attacking or casting until released.
- Verify pets withdraw, enemy hate remains, and retreat survives zoning/login.
- Verify rooted/stunned/feared bots retain the normal control-effect restrictions.

- Log out/in with equipped armor; verify appearance and item persistence.
- Use taunt and compare aggro, then toggle it off and check the report.
- Use `#bot dps cast off` and confirm DoTs/heals/buffs remain available.
- Check Harmony and other area spells before enemies have hate.
- Check mez/charm/stun caps against appropriate and over-level targets.
- Check group heals, Mass Group Buff on bots, and protected targets.
- Check DoT-only kills, backstab weapons/position, shield bash and monk specials.
- Check Harm Touch/Lay on Hands reuse across dismissal and login.

Future upstream updates require an explicit merge/rebase, conflict review, build
and regression checks. Do not automatically rebase or deploy a running server.
Preserve upstream history and license notices.

## October 12, 2026 update

Includes retreat, appearance controls, immediate full-resource revival, tankclose,
assist target, rear formations, and Challenge Mode integration. Updated bot source
matches deployed source except the intentional opt-in diagnostic guards. These
features previously passed zone builds; a new full release build has not been run.
Backattack geometry tests are included alongside the existing bot regressions.
In-game positioning, revival and raid behavior still require gameplay validation.
Apply docs/challenge-mode/migration.sql before starting this version on another
server; it adds a table without replacing character or bot data.

Publication checks: all 16 standalone bot programs and all three Challenge Mode
fixtures passed against the prepared source on October 12. The retreat test double
was updated to include the newer positioning state fields.
