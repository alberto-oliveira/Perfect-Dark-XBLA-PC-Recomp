# Test scripts

Scripted netplay tests. Each game copy on this PC follows a script, so the copies
get from the boot screens into a match with no one at the controls.

```
"tools\Build and Test.bat" -Copies 3 -Monitors 1,2,2 -Mute -Scenario basic
"tools\Run Test Copies.bat" -Copies 3 -Scenario basic        (no build)
```

`Run-TestCopies.ps1 -Scenario <name>` writes each copy's script to `test_cmd.txt`
beside its exe and starts it with the test pipe on (`--pd_test_pipe`). The game
reads the file, deletes it, and runs it line by line
(`src/perfect_dark_app.h`, `TickTestPipe`).

## How a script is put together

| Copy | Script |
|---|---|
| host (copy 0) | `lobby-host.txt`, then `<scenario>/host.txt` |
| client N | `lobby-client.txt`, then `<scenario>/clientN.txt` or `<scenario>/client.txt` |

The lobby parts take every copy from boot to the lobby. The host's part ends
hosting; each client's part ends in the host's lobby. A scenario starts from
there.

Placeholders, filled in by Run-TestCopies:

| Placeholder | Becomes |
|---|---|
| `{players}` | the number of copies |
| `{clients}` | the number of copies minus 1 |
| `{copy}` | this copy's number (0 = host) |

## Commands

Blank lines are skipped, and `#` starts a comment, either on its own line or after a command.

**Input** (pad 0, works without window focus):

| Command | |
|---|---|
| `btn <a\|b\|x\|y\|start\|back\|up\|down\|left\|right\|lb\|rb> [ms]` | press a button |
| `stick <lx> <ly> <ms>` / `rstick <x> <y> <ms>` | hold a stick (raw values, -32768..32767) |
| `trig <left> <right> <ms>` | hold the triggers (0..255) |
| `wait <ms>` | pause the script |

**Menus.** `<key>` is a menu item's address or its text id (below 0x10000). The menu trace prints both.

| Command | |
|---|---|
| `waitmenu <def\|title id\|none> [sec]` | wait until that menu is on top (`none` = no menu, e.g. the match started) |
| `pick <key> [sec]` | put the highlight on that row of the top menu, then press A |
| `pickrow <n> [sec]` | the same for the n-th row the highlight can sit on (0 = first); for rows whose text comes from a function |
| `choose <key> <index> [sec]` | open that dropdown, take option `index`: its position as shown, 0 = top (F3 on the open dropdown prints it) |

**Network and other copies:**

| Command | |
|---|---|
| `waitnet <hosting\|joined> [sec]` | wait until this copy hosts, or has joined, a match |
| `waitplayers <n> [sec]` | host: wait until the lobby has n players (the host counts) |
| `signal <name>` | tell the other copies (an empty file in `out/test-sync`) |
| `waitsignal <name> [count] [sec]` | wait until `count` signals (default 1) start with `name` |

**Checking:**

| Command | |
|---|---|
| `waitstage <n> [sec]` | wait for a stage number |
| `log <text>` | a `[test]` line in the log |
| `shot <name>` | a screenshot to `<copy>/screenshots/<name>.png` |
| `peek <addr> <bytes> <tag>` / `poke <addr> <value>` | read / write guest memory (hex) |
| `cvar <name> <value>` | change a setting for this run |

**The local player, in a match** (`src/pd_player.h`; each command waits until the
game has carried it out, at most 10 s). A `<player>` is a gamertag (`Zqhost`,
`Zqclient`, `Zqclient2`) or a number. Numbers are this copy's own and follow the
join order (the local player is always 0), so scripts are safer with names.

| Command | |
|---|---|
| `give <weapon>` | give a weapon (number, e.g. `29` or `0x1D`) and fill every ammo type |
| `equip <weapon>` | switch to a weapon the player has |
| `look <theta> <verta>` | set the view angles (degrees; verta up from the horizon) |
| `face <player> [verta] [turn]` | aim at that player's chest (or in its direction, at that vertical angle), then turn `turn` degrees (its sign picks the side) |
| `tp <x> <y> <z> [theta]` | move there (onto the floor below that point), facing theta |
| `tp near <player> <distance>` | move to `distance` units in front of that player, facing them |
| `pos [tag]` | log every player's position, room, view, weapon and knockback speed |
| `track <frames>` / `track off` | log the local player like `pos` every N level frames |
| `watchplayer <offset> <bytes>` | `watch` on the local player's struct (page-wide: noisy) |

Weapon numbers (N64 order): 1 Unarmed, 2 Falcon 2, 6 Mauler, 14 Laptop Gun, 19 Shotgun,
20 Reaper, 24 Rocket Launcher, 25 Slayer, 27 Crossbow, 28 Tranquilizer, 29 Laser,
30 Grenade, 31 N-Bomb, 35 Combat Boost, 0x51 Detonator Crossbow, 0x52 Carom LX (the
Ricochet Laser).

Every waiting command gives up after `[sec]` seconds (default 60). It then logs
`[test] FAILED <command> (stage, menu)` and drops the rest of the script, so a
stuck copy says where it stopped. Each finished command logs `[test] done: ...`.

## Recording a new menu path

Start the copies with `-TestPipe` (no scenario) and go through the menus by hand,
with the pad or keyboard. Each copy's log then has a `[test]` line for every stage,
every menu that opens (`def`, title id) and every row the highlight moves to (item
address, `#index`, text id). The row highlighted last before a menu changes is
the one that was pressed. Press F3 (with `pd_dev_hotkeys`) to log every row of the
current menu, and the selected option of an open dropdown.

## Scenarios

- `basic`: start a match with the host's saved Game Setup, and take a screenshot on each copy.
- `host-settings`: as `basic`, then log every copy's handicap bytes (with the
  `[handicap]` and `[weapons]` lines, they show the host's settings reached
  everyone), and the host walks forward for 2 seconds.
- `detonator`: as `basic`, with the host's weapon slots 7-16 all Detonator
  Crossbows (`pd_mp_weapons`), for playing the Detonator Crossbow by hand.
- `ricochet-handson` (2 copies): both players get the Carom LX, the Laser and the
  Mauler, then play by hand.
- `ricochet-look` (2 copies): screenshots of the Carom LX (white Mauler, red
  pulse and Stream) from the host and the client, and a switch away and back.
- `ricochet-flash` (2 copies): the Carom LX's red muzzle flash, then a real
  Mauler's green one.
- `gunmodel` (2 copies, start with `-ExtraArgs pd_file_override`): both players hold the
  Falcon 2; first-person screenshots (idle, firing, reloading, after a switch away and
  back) and each player's view of the other. With files in `game\DataFiles\override`
  (gun model research, `docs/research/gun-models.md`).
- `gunfar` (2 copies, `-ExtraArgs pd_file_override`): the client screenshots the host's
  third-person Falcon 2 from 250, 700 and 1500 units (texture mip levels).
- `testgun` (2 copies, `-ExtraArgs pd_file_override`, the proof gun's files in the override
  folder): the Test Gun (custom weapon 0x50, its own model) in first and third person and
  as pickups, next to a real Falcon 2.
- `ricochet-func` (2 copies): the client's copy of the host's primary/secondary
  bits (`peek`) after each toggle, for the Carom LX and the Laser.

The Carom LX's development scenarios (bounces, hits, Stream, wall sweeps) need
its debug code back: they are in `tools/debug-bank/ricochet-laser-scenarios/`.
