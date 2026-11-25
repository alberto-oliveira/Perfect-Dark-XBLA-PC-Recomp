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
