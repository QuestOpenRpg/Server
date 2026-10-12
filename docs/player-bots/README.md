# Build and install

This is the source branch. GitHub's **Download ZIP** is not the old binary installer.
Do not combine an old installer binary with these source files and assume it contains
all recent fixes. A new downloadable binary release must be built and validated separately.

## Compatibility

The baseline is EQMacEmu commit `bd838e594a29cd8d9b9bd43cd862c8692f01a10e`.
The deployed development build uses Debian 12 x86_64 and MariaDB in the EQMacEmu
Docker image. An image tagged `latest` can contain another revision.
Existing servers with custom changes need a source merge and build.

## Build from a separate checkout

Use the upstream build dependencies. Inside an environment with those dependencies:

```sh
git clone --branch player-bots --recurse-submodules https://github.com/QuestOpenRpg/Server.git takp-player-bots
cd takp-player-bots
cmake -S . -B build
cmake --build build --target zone -j 3
```

The normal output is `build/bin/zone`. This repository does not use the development
machine's `/src/build/formation-bin` output override. Do not build over an executable
currently in use. Movement diagnostics are disabled by default; developers can
configure `-DPLAYER_BOT_DIAGNOSTICS=ON` to enable bounded coordinate/packet traces
in `/tmp/takp-follow-trace.log` and `/tmp/takp-bot-packets.log`.

## Database and deployment

1. Back up the destination database and current zone executable. Keep backups private.
2. Have all players log out; stop zone launchers and zone processes while retaining
   database access. Follow your server's normal service management procedure.
3. Apply `utils/sql/player_bots/001_bot_records.sql` through `011_bot_keep_pace.sql`
   in numeric order to the destination gameplay database. Use the MariaDB client
   (scripts include delimiter directives and triggers). Stop on any SQL error.
4. Verify all eleven versions are recorded in `takp_bot_schema`. The scripts add bot
   tables and shared character/bot name reservations; they do not import player data.
   Existing name collisions must be resolved before installation can finish.
   Character and bot tables must use InnoDB for transactional name reservations.
5. Install the matching newly built zone binary using your server layout, then restart
   zone launchers. Keep the previous executable for a code-only rollback.
6. Log in and use `#bot help`. Ordinary players can use their own bots without an
   account privilege increase. Five bots plus the owner fill a six-member group.

Never import someone else's database to install this feature. For a code-only
rollback, restore the previous zone executable while zones are stopped. Retain
newer gameplay and bot tables; restoring a database backup rewinds progress.

## First bot

```text
#bot create Mercy 2 1 1
#bot spawn Mercy
#bot assist all
#bot healpercent Mercy 80
```

This creates a female human cleric. Additional examples:

```text
#bot formation on
#bot formation gather on
#bot formation line on
#bot keeppace on
#bot sit Mercy on
#bot sit Mercy off
#bot dps cast off Mercy
#bot dps cast on Mercy
#bot report Mercy
```

DPS cast OFF blocks direct-damage casting and leaves DoTs, buffs and heals eligible.
It persists per bot; omitting the name changes currently spawned owned bots.
Forced sit is a runtime hold, cleared when a bot is dismissed/recreated.

## Keeping up with EQMacEmu

Keep this feature on `player-bots`. Fetch upstream changes into a separate development
checkout, review conflicts, rebuild and run regression/gameplay checks before deploying.
Do not automatically rebase or update a running server. Preserve upstream history and
licenses. Fork publication does not imply upstream acceptance.

## Updating an existing bot installation (October 2026)

Before starting this version, apply the additive SQL in
[Challenge Mode migration](../challenge-mode/migration.sql), even if no characters
will enable the mode. Character login now loads this table. Retain all existing
character and bot tables. Rebuild the zone target; the positioning, retreat and
assist settings use the existing bot options table.
