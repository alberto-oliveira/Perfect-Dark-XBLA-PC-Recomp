# Custom weapons research

Research for four planned custom weapons, in priority order: **Ricochet Laser >
Remote Turret > Push Gun > Slow Gun**. Nothing here is implemented: this
document records what each weapon needs, what the game already does online,
and which hooks a weapon's implementation will use.

How the answers were found:

- **Statically:** from the game's code, using `tools/re` and the N64 decomp.
- **At runtime:** from test rounds T0–T7 on 2–3 game copies, driven by the test
  pipe scenarios `research-*`, now in `tools/debug-bank/weapon-research-scenarios/`
  (they need the probes back to run).
- **Probes:** the logging and experiment code (`TEMP_RESEARCH`) is in
  `tools/debug-bank/weapon-research.cpp`.

Each address below is marked **(verified)** when the game's code or a test showed
it, or **(guess)** otherwise.

## What all four weapons share

**Who decides a hit online.** Gunfire is decided on the shooter's PC:

- `chr_damage` (sub_82128200) runs there only.
- For a player victim it calls the player-hit function **sub_821274A0**, which
  applies the hit (health, knockback) and sends it.
- On the victim's PC (and every other PC) the network handlers sub_822AE858 /
  sub_822AEA98 call the same sub_821274A0 with the message's values (verified,
  T1/T2).

**What a hit message carries.** The message has:

- the two players (XUIDs at +112/+120, mapped to player numbers);
- two floats (+128/+132; for a Laser hit 0.1/0.25 on the shooter's PC, 0.094/0.25
  on the victim's);
- four bytes (+136..+139; zero for Laser and punch).

There is **no weapon number** in it (verified). sub_821274A0 works out the knockback
direction itself and adds `bondshotspeed += direction * 0.75` to the current
player (0x821277D4).

**Melee is different.** A punch runs `chr_damage` again on the victim's PC (with
the attacker's gset), so Unarmed's Disarm (FUNCFLAG_DISARM -> `bgun_disarm`
sub_8217FED0) works online (verified, T2: the victim ended up unarmed).

**Every PC recomputes a remote player's gunfire.** `shot_create` (sub_821FC8C0)
runs on every PC for every player's shot. XBLA builds a remote shot from the
hand's position (`sub_8217C1E0`) and the replicated view angles, so it is about
30 units and up to a degree off the shooter's own shot. Only the shooter's PC
applies damage (verified, T0/T3).

**The prop hit test only sees props on the current player's screen.**
`shot_calculate_hits` loops over `g_Vars.onscreenprops`
(N64 prop.c ~730; verified by the code). A shot (or a bounce) cannot hit a player
the shooter can't see.

**A direct hit works anywhere.** `chr_damage_by_laser` sub_8212DDE0(chr r3,
damage f1, vector r5, gset r6, attacker prop r7; shield on, hit part 200)
called on the shooter's PC:

- reaches the victim's PC;
- kills it there;
- gives the shooter the frag, even when the victim is off screen and 8500 units
  away (verified, T3: "Killed Zqclient2" on the shooter's HUD).

**Player numbers.** Each PC puts its own player first (players[0]), and the
others follow the join order, so the same number means different players on
different PCs. Test scripts name players by gamertag instead.

**ABI note.** A float argument takes a GPR slot: in `chr_damage(chr, f1 damage,
vector, ...)` the vector is in r5, not r4 (verified).

## Ricochet Laser (highest priority)

**Design:**

- Laser shots bounce off walls a configurable number of times.
- Each bounce raises the damage, and a hit after N bounces kills outright.
- The beam has its own colour.
- The secondary is the Stream, also bouncing.

### Findings

| Question | Answer |
|---|---|
| Where to bounce | `shot_create` sub_821FC8C0 right after its `shot_calculate_hits` call (0x821FC9B8): gunpos3d at 144(r1), gundir3d at 112(r1), hand in r31 (verified, probe A3). |
| Wall point and normal | `cd_test_los_oobok_findclosest_autoflags` sub_820E4E88 + `cd_get_obstacle_pos` sub_820DDDC0 + `cd_get_obstacle_normal` sub_820E7070 (as the Detonator's scorch fix). Floor normal came back (0, 1, 0) (verified). |
| Re-firing through the game | A second `shot_calculate_hits` sub_821FBAA8 (r3 hand, r4 1, r5/r6 camera-space pos/dir, r7/r8 world pos/dir, r9 0, f1 4294836224, stack 84 cheap) **hit the on-screen victim** with the bounce direction as its vector, and the knockback reached the victim's PC (verified, T2/T4). Camera-space copies: `sub_8214B718` (world->camera matrix, player+5992), `sub_822431A8` (transform point), `sub_822430C0` (rotate vector) (verified). It moves the hand's hit position (`bgun_set_hit_pos` sub_821776F0: player+3004 / +4960), so save and restore it. |
| Bounces at off-screen players | Not possible through `shot_calculate_hits` (on-screen props only). Use our own segment-vs-player test + `chr_damage_by_laser` (verified kill and credit, T3). |
| Damage per bounce, one-hit kill | `chr_damage`'s damage (f1) can be scaled for tagged hits (probe, verified); `chr_damage_by_laser` takes the damage directly; damage 100 killed (verified). N64 note: a shield absorbs a whole hit (any shield hit sets damage 0), so a "kill after N bounces" against a shielded player needs two hits or a no-shield call (`chr_damage_by_dizziness` sub_8212DD90 skips the shield) (guess, not tested). |
| Other PCs' view | Each PC recomputes the shooter's shot (above), so each PC can bounce it and draw the bounce itself; positions differ by ~30–50 units (verified, T3). Damage only on the shooter's PC. |
| Bounce tracer | Crossbow-bolt trail pool `g_BoltBeams` 0x8262E958 (8 × 48 B: +0 owner, -1 = free; +5 tick mode, 1 = automatic; +20 head; +32 tail; +44 speed), drawn by `boltbeams_render` sub_821844F0 with the laser texture; a slot set to automatic shrinks and frees itself (verified in code). **Seen by eye, but unreliable** (user hands-on check, 2026-11-25): the bounce shows on the shooter's screen and on the other PC, but it is thinner than the Laser beam and vanishes from some viewpoints. Walking changes it even at the same view angles. Likely cause (guess): the bolt trail is a flat, narrow strip made for crossbow bolts, not a strip that turns to face the camera like the Laser beam, so seen edge-on it disappears. For the weapon, draw bounces as real Laser beams instead: a beam from the multiplayer fireslot pool (`g_Fireslots`, N64 bondgun.c `bgun_allocate_fireslot`) or our own `beam_render` call per segment. The bolt trail stays as a fallback (two crossed strips, or a wider one). |
| Colour | `beam_render` sub_82182A30: RGBA written at 0x82182BB4 (r25), branches join at 0x82182C90 (r29 = beam, weapon at +1: 29 Laser, -2 Stream); setting the RGB there **turns the Laser beam green** (verified, T8 screenshot of the Stream). The bolt trail's colours are r7/r10 at 0x82184554 (0xAFAFFF00/0xAFAFFF7F) (verified in code). Every PC draws every player's beams through it, so a tint shows everywhere. |
| Stream | Same path, range 300 (constant 300.0 at 0x82000B08), many shots per second: each needs its own bounce test. |

### Implementation outline

1. **Registry entry.** Add the Ricochet Laser to `pd_custom_weapons.h` (base 29,
   a borrowed number such as 0x52). `AsBase` at the Laser checks in
   `beam_render` and in `shot_calculate_hits` (the `laserstream` test).
2. **Settings (host blob, like 'XBOW'):** bounce count, damage growth, kill-after-N.
3. **Hook at 0x821FC9B8, for every PC.** If the shot is this weapon's:
   - walk up to N bounces with the line test;
   - draw each segment as a Laser beam (fireslot beam or our own `beam_render` call; the bolt trail is too thin, see Bounce tracer);
   - only on the shooter's PC (players[0] fired), test each segment against
     every player and apply `chr_damage_by_laser` with the bounce's damage;
   - the first segment is the game's own shot, so let the game damage it.
4. **Tint:** hook 0x82182C90 (beam) and 0x82184554 (bounce trails), keyed by
   the custom weapon number.

**Feasibility: high. Difficulty: ★★★☆☆** (lower than estimated: no network
code is needed).

### Implementation notes (branch ricochet-laser, 2026-11-25): the Carom LX

The weapon is named the **Carom LX** (a carom: a shot that rebounds, in billiards).

Findings from the user's hands-on checks, after the first build:

- **Walls must be found as bullets find them.** The collision geometry (`cd_*`)
  stops below high walls and ceilings nobody can reach: shots there made sparks
  but no bounce (54 of ~200 pulses in one session). Bullets test each room's
  VISIBLE triangles: `portal_find_rooms` sub_820DC1A8(from, to, fromrooms,
  finalrooms, crossed, 30) for the rooms the line crosses, then
  `bg_test_hit_in_room` sub_820D4DC8(from, to, room, &hitthing) (hitthing +0 the
  point, +0x0C the surface normal). Each bounce now uses that test, starting in
  the room of the wall it left; the collision test is only a fallback.
  `RicoFindWallVisible` in main.cpp.
- **The first bounce starts at the game's own hit point**, the hand's hit
  position (hand +1372: player+3004 / +4960), where the beam ends and the sparks
  are.
- **Hits are tested as the streak travels, not when the shot is fired.** A third
  bounce arrives ~0.25 s after the shot, so a player walking into it was visibly
  hit for nothing. Each frame (from the player input step) the stretch a pulse's
  streak newly covered is tested against the players where they are then; a hit
  ends the streak there and drops the shot's later bounces. The Stream (whole
  beams for 2 frames) is still tested at once.
- **Sparks at each bounce**: `sparks_create` sub_8215CD58(room, prop 0, position,
  direction, normal, type 0), when the streak reaches the wall.
- **The red beam looked thin**: the Laser texture's glow is blue, which the red
  takes out, leaving the core. The game's other beam textures were thinner
  still. The beams are drawn wider instead (`pd_rico_beam_width`, default 200%;
  f29 at 0x82182C90 is the width).
- **The muzzle flash** is a part of the gun model drawn in the OPAQUE list as
  texture x vertex colour (combine `FC121824 FF33FFFF`); XBLA's texture 0x0296 is
  green. Neither the render colour, a translucent-pass split nor changing the
  vertex colour at draw time reaches it (XBLA prepares the model when it loads).
  What works: the gun loader hook 0x82170524 (right after file_load_to_addr; r30
  = the file, r31 = the player) sets the flash's vertex colour in this equip's
  own copy of the file, found by the pattern {0xFEFEFEFF, 0, 0x05xxxxxx} (the
  loaded file's layout differs from the dump of 0x379). A real Mauler loads its
  file again and stays green. Other players see the Carom's flash green (the
  third-person model is shared with real Maulers).

Earlier notes:

Built as planned (`src/pd_ricochet.h`, toml section "Ricochet Laser"), with these
findings on the way:

- **Look:** weapon 0x52 is a Laser that looks like the Mauler. Its definition takes
  from the Mauler's the model files, equip animations, position and part visibility
  (+0x00..+0x13, +0x28..+0x43). The MP entry takes the Mauler's pickup model
  (+6..+9), and the third-person model hook sees 6 (verified).
  (A first version dropped the Mauler model's green flash with copied function
  definitions with `FUNCFLAG_NOMUZZLEFLASH`, 0x2000 at +0x10; it is now drawn
  red instead, see above.)
- **Laser checks mapped (verified):**
  - `bgun_create_fx` 0x82174E70 (r26);
  - hit sounds 0x82176C64 (r7) and 0x821772AC (r11);
  - `gset_populate` 0x821863D4 (r8);
  - `shot_calculate_hits` 0x821FBC0C (r10) and 0x821FC268 (r11);
  - `obj_hit` 0x82222608 (r11);
  - pickup sound 0x8220C614 (r4);
  - `beam_create_for_hand` 0x82182428 (r11).

  `bgun_update_laser` (0x82175810) is left out, because it moves the Laser model's
  own parts.
- **Beams:** a beam is 44 bytes. `beam_tick` = sub_82183688. `g_Fireslots` =
  0x8262E560 (48 B; beam at +4; chr+380 = fireslot). The Laser's `beam_create`
  values:
  - length capped at 10000, minimum 500;
  - speed 0.25 × length (4 frames to cross);
  - streak 0.6 × length (at most 3000);
  - `dist` starts negative.

  Bounce beams come from a pool of our own, drawn after `boltbeams_render` in the
  player render (0x820FC7A4, r3 = display list) with `beam_render(gdl, beam, 0, 0)`.
  This works on every PC, from any angle (verified).
- **Bug found and fixed, in all custom weapons:** four primary/secondary bit hooks
  ran after the game had already computed the bit's mask (`clrlwi` before
  `srawi`). As a result, a remote player's choice set the wrong bit on the other
  PCs, so the Stream replayed as pulses. They now run on the `clrlwi`
  (0x82107044, 0x82180BA0, 0x8217D2C0, 0x8217D49C). Remote PCs keep only the
  current weapon's bit of a remote player (vanilla).
- **Hits:** a player's health and shield are applied on that player's own PC. The
  shooter's copy of a remote chr does not change. Two `chr_damage_by_laser` calls
  kill through a shield (verified, test R3). Self-hits kill (verified).
- **Cost:** about 15–25 µs per shot (verified, test R4).
- **Arena note:** the stage 50 corridors have no ceiling collision, so floor
  bounces fly to the sky.

**Tests it will need:**

- 2–3 copies: bounces visible on every PC, from any angle (the bolt-trail tracer failed this);
- hits on and off screen;
- frag credit;
- damage growth and the kill-after-N rule;
- the Stream's cost per frame;
- `tp`/`face` scenarios like `research-ricochet2`.

## Remote Turret

**Design:**

- The primary throws a turret (like the Laptop sentry).
- The secondary switches the view to it, to aim and fire.
- Leaving the turret makes it explode.

### Findings

| Question | Answer |
|---|---|
| Deploy | `laptop_deploy` sub_8221E5C8 (returns the obj, r31 at 0x8221E934); callers: local throw sub_8217EEA8, replay of a remote throw sub_82170F28, bots sub_820CD218 (verified). Autogun fields as N64: yzero +96, ymaxleft/right +100/+104, yrot +108, xzero +116, xrot +120, maxspeed +128, firing +94, beam +156, target +164, ammo +169 (verified). |
| Online | Every PC makes its own copy; only the owner's copy has ammo and a target and does damage (RCP45, weapon 43). The other copies follow the owner's aim and firing (verified, T4). Landing spots differ by 200–300 units per PC (thrown physics are not synced). |
| Aim sync | `autogun_tick` = **sub_82206E58** (the object tick sub_82227B30 calls it for type 13 objects, 0x82228534) (verified). Online it computes goal angles on its stack (84(r1), 88(r1)); the owner SENDS them with the firing flag (sub_82290770 at 0x82207B6C), the copies RECEIVE them (sub_8228DBF0 at 0x822078E4); r18 != 0 = a receiving copy (verified). |
| Steering | Overriding the goals at 0x822078E8 and firing at 0x82207B08 on the owner: the owner's sentry turned and fired where told, **and the other PC's copy followed** (verified, T7). Goal 88(r1) lands in the +120 field, 84(r1) in +108. Skipping `autogun_tick` instead (T6) turned only the owner's copy.  User hands-on check (2026-11-25): the steered sentry turned to the set angle as soon as it stuck to the wall, and it fired at once, because the probe forces the firing flag every tick while steering is on. In the weapon, firing follows the controlling player's trigger. The sentry was also tinted: the tint probes colour every bolt trail, not only the Laser's, so a weapon's tint must check whose trail or beam it is. |
| Slayer view | `player_launch_slayer_rocket` = sub_820F9138: player+620 rocket, +16 vision mode (2), clears the vision devices at +196 (verified). The fly-by-wire rocket follows the same path on every PC (verified, T4). The Slayer branch steers a *projectile*, so a sentry needs its own camera hook (the call that places the camera at the rocket, `player_move_camera_from_pos_rooms` in N64 player_tick; XBLA address not found yet). |
| Explode on leaving | `prop_explode` sub_82205A18 on the owner's PC, as the Detonator does (verified in the crossbow mod). |

### Implementation outline

1. **Registry entry:** base 14 (Laptop Gun); primary = the deploy function.
2. **Secondary = "control":**
   - set a per-player state;
   - each tick, map the player's view angles to the sentry's goal angles
     (0x822078E8) and the trigger to its firing flag (0x82207B08);
   - the game syncs both.
3. **Camera:** a hook in the player's camera placement while controlling (the
   body stands still, as with the Slayer).
4. **Leaving:** `prop_explode` on the sentry.

**Feasibility: medium-high** (was low-medium). The sync works with no network code
of ours; the camera is the remaining unknown. **Difficulty: ★★★★☆.**

**Tests it will need:**

- the camera hook (by eye);
- angle mapping (the goals are relative to the sentry's `yzero`);
- remote copies following;
- the explosion on exit;
- what happens when the owner dies while controlling.

## Push Gun

**Design:**

- Shotgun-like, with a long wind-up.
- Little damage, strong knockback.
- The victim also becomes dizzy and drops its weapon.

### Findings

| Question | Answer |
|---|---|
| Knockback online | Arrives on the victim's own PC through sub_821274A0 (verified, T0: 0.75 for a Laser hit, capped at 1.5 for three Shotgun pellets; ~70 and ~250 units of slide). |
| Strength | Multiplying the 0.75 on the victim's PC (f0 at 0x821277D4) ×4 and raising the cap in `bmove_dampen_shotspeed` sub_820F2098 (f12 = 1.5 at 0x820F20CC) to 6: **~1000 units of slide** instead of ~80, even up a slope (verified, T5). The movement is horizontal only (no vertical launch). |
| Telling a Push Gun hit apart on the victim's PC | The message has no weapon number. Options: a damage value no other weapon uses (f1/f2 reach the victim scaled, e.g. 0.094/0.25 for a Laser hit); or make the Push Gun's shots melee-type, which replay `chr_damage` on the victim's PC with the gset (guess, not tested). |
| Drop the weapon | Vanilla Disarm works online for melee hits (verified, T2). For gunfire, `bgun_disarm` runs only on the shooter's PC (on the victim's copy, verified T2), so the victim's PC must do it, from the hit (same identification problem as above). `gset_has_function_flags` = sub_821866B8(gset, flag) (verified). |
| Dizziness | `gset_get_blur_amount` sub_82116010: 1000 online (250 offline); weapon 28 Tranquilizer 2000, 86 bolt 5000, 31 N-Bomb 100; only `chr_damage` calls it (verified). The crossbow's dizzy bolt reaches victims online (crossbow mod). |
| Recoil Jump secondary | Our own `bondshotspeed` (player+388/+396) on our own PC: nothing to sync (verified knockback moves the player). |
| Fall deaths | No fall damage; deaths from `GEOFLAG_DIE` floors or falling for 4 s (N64 bondwalk.c). Which arenas have such drops is still open. |

### Implementation outline

1. **Registry entry:** base 19 (Shotgun) or a melee-type function.
2. **Wind-up:** the Reaper's spin-up or the Mauler's charge (N64 `gs_barrelspeedfrac`
   / Mauler charge; XBLA addresses not looked up yet).
3. **On the victim's PC (hook in sub_821274A0)**, when the hit is a Push Gun hit:
   - scale the knockback and raise the cap for a moment;
   - drop the weapon (`bgun_disarm`);
   - add the dizziness.
4. **The open design choice:** how the victim's PC recognises the hit (a damage
   signature, or melee-type shots). This needs one test round.

**Feasibility: high. Difficulty: ★★★☆☆.**

**Tests it will need:**

- recognising the hit;
- slide distances;
- the drop;
- the dizziness;
- the Recoil Jump;
- pushes into the arenas' drops.

## Slow Gun

**Design:** the player who is hit is slowed for a long time, and it lasts through
death.

### Findings

| Question | Answer |
|---|---|
| Combat boost online | Per player on XBLA: player+7324 time left, +7328 on; `bgun_add_boost` sub_82178668 (cap 18000). The boosted player's move tick sub_820F4D40 doubles lvupdate240/60 (g_Vars+52/+56) and scales lvupdate60freal (+76) for that player only, putting them back at 0x820F56C4 (verified). Boost works online and the flag is visible on other PCs (verified, T2). |
| Hidden speed value | The walk speed functions sub_821030C0 / sub_821031C0 read the player config's u16 at +0x1C, speed × (v + 25) / 100, default 75 (verified). Poking 0 into the local config slowed walking to ~20–25 % (verified, T2). Each PC is config 0 for itself: 0x8264196C. |
| Slow clock | Scaling the local player's clock ×0.5 in the move tick (0x820F4DB0) halved walking (verified, T5) but **not the fire rate** (Reaper 19 vs 18 shots/s): the gun's timing uses something else, still to find. |
| Hit signal | Same as the Push Gun (no weapon number in the hit message). |
| Lasting through death | A timer of our own on the victim's PC; the respawn (sub_820FDA68) only clears poison and blur. |

### Implementation outline

1. **On the victim's PC**, when hit: set its own config +0x1C (walk speed), and/or
   scale its move-tick clock, for N seconds.
2. **Show it:** combat boost's screen effect, or a HUD timer.
3. **Fire rate (optional):** find the gun's clock.

**Feasibility: high. Difficulty: ★★☆☆☆** (walking); ★★★ with fire rate.

### Slow Gun: static research (branch slow-gun, 2026-11-28)

The weapon is built as **weapon 0x44** (name text 0x4CE3). It works like the **Mauler**
(base 6) and looks like the **DY357 Magnum** (look 8). Its secondary is the Mauler's
charge, which drains the whole clip. Everything below comes from the generated code and
the N64 decomp; none of it has been tested in game yet.

**Hit signal: the blur code.**
- At the end of `chr_damage` (0x8212A500-0x8212A558), the game works out a "blur code"
  for weapons with the dizzy function flag (512): Tranquilizer → 1, bolt (86) → 2,
  N-Bomb → 3.
- It passes the code as **r10** to **sub_822ABD68**, the network send of the hit
  (`bl` at 0x8212A598; r3 = r28, r4 = r26, f1/f2 = the damage values, gset in r22).
- On each receiving PC the code reaches the player-hit function sub_821274A0 as its
  stack byte. It is read at **0x821278C0** (`lbz r10,375(r1)`): values 1/2/3 add blur
  to chr+728 (r28 = the chr), any other value adds nothing.
- The shooter's own call (0x8212A760) passes r24 (= 0) there instead.
- **Plan A:** at 0x8212A598, a Slow Gun gset gets r10 = `0x40 | charge byte`. The
  charge byte is gset+2 (`maulercharge`, the charge × 10, 0-50). At 0x821278C4 the
  receivers read the marker and turn it back to 0.
- **Probe P1 (verified, 3 copies; logs host 274-275, client 231-232, client2 053-054):**
  - **The message keeps only 2 bits of the blur code.** 0x40 arrived as 0, 0x72 as 2,
    0x6B as 3.
  - **Only the victim's PC replays the hit.** A third PC logged nothing for hits on the
    other client.
  - The send's **r9** is the disarm flag (`gset_has_function_flags(gset, 0x400)`,
    FUNCFLAG_DISARM, at 0x8212A4FC). It travels too.
  - The game never sends disarm + a blur code, since only weapons 28/31/86 have blur
    codes. So the Slow Gun sends **disarm = 1 + blur code = its tier** (1 shot,
    2 part-charged, 3 full charge).
  - The victim's PC catches the pair at the **entry of sub_821274A0** (r10 = disarm,
    the blur code at 87(r1), r3/r4 = attacker/victim in its own numbering) and clears
    both before the game disarms or blurs anyone.
  - Result: all 5 marked hits arrived with the right tier on the right PC, and the
    Tranquilizer's code 1 arrived unchanged.

**Mauler checks (`cmplwi rX,6`).** Each one needs AsBase for 0x44 unless noted:

| Function | Site | Register | What it does |
|---|---|---|---|
| `gset_populate` sub_82186358 | 0x82186370 | r8 | Stores the charge × 10 in gset+2. r8 is also the Laser test at 0x821863D4. |
| `gset_get_damage` sub_821864A8 | 0x82186540 | r9 | Damage × (gset+2 / 3 + 1). |
| `bgun_tick_gameplay2` sub_8217CD40 | 0x8217CED0 | r11 | Calls `bgun_tick_mauler_charge` sub_82175F98. |
| sub_82179A40 (can fire again) | 0x82179C60 | r10 | Resets the charge after the shot. |
| sub_82179620 (shot sound) | 0x821799B4 | r10 | Lower pitch when charged (cosmetic). Check that r10 is dead. |
| `beam_create_for_hand` sub_821823A8 | 0x8218245C (r11), 0x821825F8 (r6) | | Beam type −3 − charge (green tracer textures). |
| `bgun_create_fx` sub_82174CC0 | **0x82174E38** | r26 | The switch that makes the tracer. The existing AsBase at 0x82174E70 sees only numbers above 28, so a Mauler-based weapon needs this earlier site. |
| gun renderer sub_821761F8 | | | Blends the gun's colour towards 0xFF00007F by charge × 50 (hand+572). The Slow Gun's tint goes here. |

**Charge (`bgun_tick_mauler_charge` sub_82175F98).**
- The loop runs per hand, with r31 = hand+572 (`mm_maulercharge`, a float that shares
  its place with `mm_lasertype`).
- loadedammo[0] = hand+544. Charging needs more than 1 round loaded.
- Each frame adds 0.05 × lvupdate60freal (f24), capped at 5.0 (f25).
- On each whole-number step, `loadedammo[0]--` runs at **0x821760A4**. The whole-clip
  drain goes there: keep the 0-5 scale and take ceil((loaded − 1) / (6 − level)) rounds
  per step, so that 20 rounds = 4 + 4 + 4 + 4 + 3 and the shot uses the last one.
- With the primary selected, the charge wears off at 0.005 per frame.

**Clocks.**
- The move tick sub_820F4D40 scales g_Vars+52/+56/+76 for a boosted player around the
  input step (`bl 0x820f2988` at 0x820F4DB0) and restores them at 0x820F56C4.
  Movement and stick look run inside that window.
- **The gun ticks elsewhere.** The per-hand tick sub_82174EC8 is called only from
  `bgun_tick_gameplay2` sub_8217CD40. That function is called only from the
  per-player function sub_820FC6E0 at **0x820FC798**, which also draws the bolt
  beams and the gun.
- So the fire-rate slow = scale the clock before 0x820FC798 and restore it at
  0x820FC79C. This covers firing, charging and the gun animations.
- Automatic fire uses lvupdate60freal (rpm × freal / 60); the recovery times count
  `stateframes`.

**Mouse look.**
- Online, the mouse turns through the port's own `OnMouseLookDelta`
  (src/perfect_dark_app.h). There, `net_look_theta_` / `net_look_verta_` add up
  dx/dy × step and are sent with the input.
- Slowing the mouse aim = scale `step` for the local player. No guest hook.

**Models.**
- DY357 Magnum first person: hi 0x37A, lo 0x4BD. The lo file links mesh **0x9DA**
  (texture 0x14BE, 7 groups: frame + 6 rounds); part 0 (link 0xFFFF) is the muzzle
  flash, drawn from the N64 display list.
- Third person: model 0xF8, file 0x34B, linking mesh 0x83C (textures include the
  flash 0x970).
- Borrowed model for the Slow Gun: **0x1A1** (MODEL_CHRDOORDECODER, scale 409). No
  weapon's third-person model and no MP pickup uses it; only the solo setups of
  dataDyne Extraction and the Institute place it.
- 0x13 (data uplink) is ruled out: Hack That Mac uses it.

### Slow Gun: implementation notes (branch slow-gun, 2026-11-28)

Built as planned: `src/pd_slow.h`, the toml section "The Slow Gun", and
`tools/models/make_slowgun.py`. All of this was verified in scripted tests R1-R4 and
C1-C3, except where noted.

**The weapon.**
- Weapon 0x44 works as the Mauler (base 6) and looks like the DY357 Magnum (look 8).
- Its two functions are copies of the Mauler's with `unk27 = -1` and `recoverytime60 =
  60`, so it fires once per ~1.25 s (with `unk27 >= 0` a new press fires after only
  `unk24 + unk25` frames), at half the damage.
- The Mauler checks listed above get AsBase, except `gset_get_damage`: a full Mauler
  charge multiplies the damage by about 18 and killed outright. A Slow Gun charge buys
  duration and blur instead.
- The charge drains the whole clip: each of the 5 steps takes its share at 0x821760A4,
  local hands only, since every PC also ticks its copies of the other players' guns.

**Tracers.**
- A Mauler shot's beams take the gun's weapon number. An unknown number gets
  `beam_render`'s orange texture, and a violet colour on orange came out pinkish red.
- So the Slow Gun's beams are noted at `beam_create` (the Carom LX's hooks) and get the
  Cyclone's number (11, the blue texture).
- They are drawn violet, opaque, and (1 + charge) times as wide.

**The hit.** Disarm + blur code, as found in P1 above, on the victim's PC only.

**The slow** (the victim's own PC):
- movement and stick aim: the move tick's clock, scaled at 0x820F4DB0;
- the gun: the clock is scaled around `bgun_tick_gameplay2` (0x820FC798 / 0x820FC79C);
- mouse aim: the port's mouse-look step;
- blur: chr+728 topped up.

The integer clocks keep their remainder from frame to frame, so a 50% clock alternates
1, 0, 1… Measured: walking 715 → 322 units, stick turning 156° → 78°, Reaper ~29 → ≤14
rounds/s. Mouse aim was not testable by script.

**Death.**
- The timer runs only while the player is alive (`player+224`), so a slow carries into
  the next life.
- Death ends the poison.

**Poison.**
- One tick a second: `chr_damage_by_dizziness` (sub_8212DD90), called from the player's
  input-step hook with the attacker's prop.
- 0.3 damage ≈ 3.75% of health; full health is about 8 damage units (`bondhealth`,
  player+228).
- A poison kill credits the shooter ("Killed Zqclient" on the shooter's HUD).
- The poison's own `chr_damage` sends a hit too; a flag keeps it from being marked as a
  new Slow Gun hit.

**Host rules.**
- Host blob "SLOW": 9 u16s (duration, full-charge factor, move/aim/fire %, two blur
  levels, poison seconds and damage).
- Joined PCs use the host's rules (R4: the host's 20 s over a client's own 5).

**The tint.**
- Only the victim's PC gets the hit, so each PC marks a slowed player itself from its
  replay of every shot.
- At 0x821FC9B8, r3 is **not** the prop hit (it was 0 for a hit). The mark comes from
  a capsule test from the shot's start (144(r1)) to the game's hit point (hand+1372),
  skipping the shooter.
- The overlay is added in the prop colour hook (0x821E1900) for player props (type 6).
- All three PCs marked the right player.

**Model.**
- The Magnum's first-person cylinder is the vertices of bone 34 (the hammer is bone 38).
- The third-person mesh is rigid: x along the barrel (muzzle at -120), y the width,
  z the height.
- The first-person flash names its texture in its display list (`C0580004` + number):
  the Magnum's 0x39B becomes the Mauler's green 0x85E. The third-person `chrgunfire`
  texconfig becomes 0x96F.
- The Magnum's grip panels are grey in the texture, so a rectangle of the atlas is
  recoloured.
- A pistol's muzzle flash lasts too short for the scripted screenshots, the real
  Magnum's too, so the flash colour was not seen in a test.

**Test scripts.**
- Signals sent by several clients need names with `{copy}` (`armed-{copy}`).
- `tp near` can put two players on the same spot.

## Engine findings from the Remote Turret work (branch remote-turret, 2026-11-27)

The Remote Turret is on hold on its own branch (`remote-turret`): its implementation
notes are there. These findings apply to any custom weapon. All were verified in tests
unless marked otherwise.

**Player struct offsets (XBLA vs N64).**
- No shift up to +0xC8: `visionmode` +16 (u16), `devicesactive` +196, `badrockettime` +200.
- +8 from about +0xD0 on: `isdead` +224, `speedsideways` +368, `speedstrafe` +372,
  `speedforwards` +376, `vv_theta` +332, `vv_verta` +348.
- +0x28 in the gun-control area: `ammoheldarr` (reserve ammo by type, s32) +6096.
- A hand (player +1632, 1956 bytes each): +3 `gset.weaponfunc`, +12 `triggeron`,
  +544 `loadedammo[2]`, +1540 state.
- Hand +1708 is the N64 `attacktype` (guess).
- `bondactivateorreload` +208 is NOT set by this port's Reload button.

**Ammo.**
- Multiplayer ammo crates have slots only for ammo types 1-19 (`setup.c`:
  `priammotype < 20`). A custom ammo type for crates must be below 20.
  `AMMOTYPE_DART` (0x11) is used by no weapon.
- `weapon_get_pickup_ammo_qty` (sub_8220CB10) knows only the game's own types and gives
  1 round for any other. Its exit is 0x8220CD60 (r3 = the rounds, r31 = the weapon
  object).
- `g_AmmoTypes` is at 0x825CDAA8: 12 bytes per type, s32 capacity = the reserve's maximum.
- ammodef: +0 type, +4 casing, +8 s16 clip, +0x0C reload animation, +0x10 flags.
  `AMMOFLAG_NORESERVE` (1) makes every gain go into the clip. With it,
  `bgun_set_ammo_quantity` ADDS to the clip instead of setting.
- `bgun_get_ammo_qty_for_weapon` sub_82177A50 / by type sub_821778A8 = clip + reserve.
- The MP weapon entry: +1 primary ammo type, +2 quantity (crates).
- Reload reaches the gun through `bgun_reload_if_possible` sub_8217CCA8 (hand in r3),
  called by the level tick sub_8218AB68.

**Throwing.**
- A throw without `FUNCFLAG_DISCARDWEAPON` goes through the ordinary throw, which does
  `loadedammo[ammoindex]--`: a throw function needs ammoindex >= 0.
- The thrown-projectile maker is sub_821709E8 (local throws, the replay of remote ones,
  bots).
- A special function (`INVENTORYFUNCTYPE_SPECIAL`) with an unknown special number fires
  with no ammo and does nothing: a clean "function the mod handles itself".

**The trigger.** `bmove_process_input` passes the local player's trigger to
`bgun_tick_gameplay` at 0x820F4190 (r3). Holding it at 0 there stops the gun firing.
- The input step that toggles the function reads the trigger as off for one step.
- 0x82180B60 (XBLA's input exchange) handles the other players' triggers.

**Camera, view and body.**
- `player_tick`'s normal branch places the camera at 0x821005E0: r3 = the position (a
  stack copy), r6/r7 = the position and rooms it is checked from. The look and up stay
  the player's.
- The walk call `bwalk_apply_move_data` is at 0x820F4494 (skip to 0x820F449C to freeze
  the body; the view still turns).
- The first-person gun renderer sub_821761F8 is called once a frame with &gdl. It draws
  the hands at 0x821769A8..0x821769EC. The gun's place in the view comes from weapondef
  +0x2C/+0x30/+0x34 (posx/posy/posz), read every frame.
- A player's own body is drawn only for the eyespy (camera mode 2) or the Slayer view:
  `player_tick_third_person` sub_820FD4A8 reads the camera mode at 0x820FD6C4 (r11; r30 =
  the player).
- The Slayer rocket's "monitor" picture: the level render checks `visionmode == 2` at
  0x8218BA40 (sub_8218AB68), and its sounds are checked per player at 0x8218A4B8.
  Visionmode 3 draws one frame of static and resets itself.
- A player's held gun in third person: before its `model_render` at 0x8210A0A8 (r31 = the
  prop, r28 = the chr; jump to 0x8210A0B0 to skip it).
- `OBJFLAG2_INVISIBLE` (flags2 +12, 0x80000) hides an object on this PC only.

**Cloak.**
- The cloaking device = `devicesactive` |= 0x40, plus cloak ammo (type 0x14,
  `ammoheldarr`, in 1/60 s).
- Granted by code on the owner's PC, every PC sees the player cloaked
  (`CHRHFLAG_CLOAKED` 0x20000000 at chr +0x14).
- Throwing or firing pauses it (vanilla).

**Laptop sentries (autoguns) online.**
- The owner bits (obj +64 >> 28) are the thrown-laptop slot, the same on every PC, not a
  player number.
- The goal angles are world angles, 84(r1) = yrot and 88(r1) = xrot (`apply_rotation` at
  0x822078F4). The shot runs along (cos x · sin y, sin x, cos x · cos y).
- `autogun_tick_shoot` sub_82207DC0 shoots from the barrel tips only when the sentry is
  on screen, otherwise from its centre. Its shot start (168(r1)) is known on both paths
  at 0x82208058 (r28 = the object).
- The sentry's model turns its gun about the wrong axis on a sloping surface: the
  barrel comes off its body.
- An explosion's network message names the source prop by its sync id (prop +44;
  negative ids are skipped). For a sentry the receivers set `OBJFLAG_DEACTIVATED`
  instead of deleting it.
- Using one's own thrown sentry (sub_822211E8) gives back a Laptop and its rounds as SMG
  ammo.

**Models.**
- XBLA mesh texture coordinates run v up (texel row = (1 - v) × height). Triangles are
  clockwise seen from the front.
- New parts can be added to a mesh directly in Python (`xbla_format.py`): append
  vertices rigid on one bone and triangles to a group's last sub-mesh.
- Borrowed model numbers have their own scale: model 0x19F's is 4096, ten times a gun's
  409. `pd::custom::Install` now gives a borrowed number the replaced model's scale.
- Model 0x19F (MODEL_CHRLUMPHAMMER2) is free in multiplayer: no pickup, no
  third-person model.
- Laptop Gun files: first person 0x385/0x4C7 -> mesh 0xA22 (texture 0x1591); third
  person model 0x105 = file 0x358 -> mesh 0x855 (texture 0x0FC4).
- Free archive ids so far: 0x271/0x272 and 0x27F upward; 0x27B-0x27E are used by
  `remote-turret`.

**Test scripts.** A script cannot "use" an object with `btn x`: X is Reload in this
port.

## Test tools added (branch weapon-research)

The pipe commands `give`, `equip`, `look`, `face <player|gamertag> [verta]`,
`tp`, `tp near`, `pos`, `track` and `watchplayer` (`src/pd_player.h`) are
described in `tools/test-scripts/README.md`. The research scenarios are
`research-tools`, `-hits`, `-ricochet`, `-ricochet2`, `-turret`, `-push`,
`-sentry`, `-tint` and `-handson`. They and the probes they switch on are in the
debug bank (`tools/debug-bank/weapon-research-scenarios/`,
`tools/debug-bank/weapon-research.cpp`); to run one again, restore the probes and
copy its folder back to `tools/test-scripts/`.
