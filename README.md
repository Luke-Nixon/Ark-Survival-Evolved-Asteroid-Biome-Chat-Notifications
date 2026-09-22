# Asteroid Biome Chat Notifications

An ARK: Survival Evolved server plugin for **Genesis 2**. When the space biome
rotates, it announces what the new asteroid field contains, in cluster chat:

```
INFO (Asteroid Zone) [Gen2]: A new asteroid biome has been reached. Asteroid Biome contains: Crystal, metal and green supply crates.
```

## It has no database

This plugin holds **no database connection and no credentials**. It asks
[ArkCrossChat](https://github.com/Luke-Nixon/Ark-Survival-Evolved-Cross-Chat-System)
to say the line, through an exported function resolved at call time.

That is deliberate, and it is the main thing to understand about this version.
It used to open its own MariaDB connection and `INSERT` a row directly into
ArkCrossChat's `messages` table — one insert, no reads, no state — and for that
it carried a database library, a password in its own `config.json`, reconnect
logic, and a socket opened **on the game thread inside a UFunction hook**. A
slow database stalled the server mid-dispatch.

Removing it means the `messages` table has one fewer writer, which matters when
that table moves between database engines: fewer independent programs have to be
coordinated at the cutover, and this plugin never needs porting again.

## Requires ArkCrossChat

**ArkCrossChat must be installed on the same server**, and it must be new enough
to export `CrossChat_Post`. Without it this plugin runs, hooks nothing harmful,
and logs a warning for each announcement it could not send:

```
[asteroid] ArkCrossChat is not loaded - notification not sent
```

## Installing

1. Build (see `BUILDING.md`), or take a release.
2. Copy the `AsteroidBiomeChatNotifications` folder into
   `ShooterGame/Binaries/Win64/ArkApi/Plugins/`.
3. Optionally copy `config.example.json` to `config.json` and edit it.

There is nothing to configure to make it work. Only Genesis 2 needs it — on any
other map the day-cycle blueprint does not exist, so the plugin finds nothing to
watch and stays idle.

## Configuration

`config.json` is optional; every key has a working default.

| key | default | |
|---|---|---|
| `sender_name` | `INFO` | the name the announcement appears under |
| `tribe_name` | `Asteroid Zone` | the tribe shown beside it |
| `message_prefix` | `A new asteroid biome has been reached. Asteroid Biome contains: ` | put in front of the description |
| `biomes` | the eight below | indexed by the game's zone number |

**`biomes` is order-sensitive and is replaced wholesale, not merged.** The
game's `NetEndWarp` gives a zone index, and that indexes this list. A partial or
reordered list announces the wrong biome, so one malformed entry rejects the
whole list and the built-in defaults are used instead — loudly, in the log.

## Building

See [BUILDING.md](BUILDING.md). It needs the ArkServerApi framework and nothing
else: no database client, no connector, no patches.
