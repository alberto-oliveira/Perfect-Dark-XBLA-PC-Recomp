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

### Implementation notes (branch remote-turret, 2026-11-27)

Built as weapon 0x42 (`src/pd_turret.h`, toml section "Remote Turret"); placeholder
name "Remote Turret". Findings on the way:

- **Throwing without losing the gun.** The weapon's primary is a copy of the Laptop's
  Deploy without `FUNCFLAG_DISCARDWEAPON`. Without that flag the throw goes through the
  game's ordinary throw (`bgun_tick_inc_attacking_throw`), which takes one round from
  `loadedammo[ammoindex]`: the Deploy's ammo index -1 would write outside the array, so
  the copy uses 0. The thrown-projectile maker sub_821709E8 checks for the Laptop
  (`cmplwi r11,14` at 0x82170B28) before calling `laptop_deploy`; the sentry comes back
  in r3 at 0x82170B34. This runs on every PC (local throw, remote replay, bots).
- **The sentry's owner bits are not a player number.** `laptop_deploy` puts the
  thrower's thrown-laptop slot there, the same number on every PC (the host's turret had
  owner bits 0 on the client too, where the host is player 1). The owner is the current
  player at the deploy hook instead.
- **Goal angles:** 84(r1) is the yrot goal and 88(r1) the xrot goal (`apply_rotation`
  at 0x822078F4). The angles are world angles: the shot runs along
  (cos x · sin y, sin x, cos x · cos y), so the goals come from the player's look
  vector. The owner's PC also sets yrot/xrot directly, so its view is the turret's at
  once; `maxspeed` (+128) is raised on every PC so the copies keep up. An idle turret's
  goals are its own angles, and its firing flag is cleared.
- **Second function "Control":** a special function (`INVENTORYFUNCTYPE_SPECIAL`) with
  special number 20, which the hand's attack switch (1-12) ignores; it fires with no
  ammo.
- **The trigger:** `bmove_process_input` passes it to `bgun_tick_gameplay` at 0x820F4190
  (r3). The mod reads it there, and holds it at 0 while the player controls a turret.
  Otherwise the Control function's own attack is under way, and a toggle back to the
  primary turns it into a throw. The hold also lasts after leaving, until the trigger
  has been let go for 6 steps: the input step that toggles the function reads the
  trigger as off for one step. The store at 0x82180B60 (XBLA's input exchange) is for
  the other players only.
- **Camera:** `player_tick`'s normal branch calls `player_move_camera_from_pos_rooms`
  at 0x821005E0 (r3 = the position, a stack copy; r6/r7 = the position and rooms it is
  checked from). The mod swaps in the turret's position, moved out of its surface along
  its up axis (`pd_turret_eye_offset`), and its rooms. Look and up stay the player's,
  so mouse look works as usual.
- **Frozen body:** the walk-mode call of `bwalk_apply_move_data` (0x820F4494 ->
  0x820F449C) is skipped and the speeds zeroed; turning is done outside it.
- **Hidden gun and turret:** the gun renderer sub_821761F8 returns at once (it is called
  once a frame with &gdl). The turret gets `OBJFLAG2_INVISIBLE` on the owner's PC only.
- **Leaving:** `prop_explode(prop, 13)` on the owner's PC. The explosion message carries
  the prop's sync id (+44), and the receivers handle the prop with that id
  (sub_822AFA30 -> sub_821FA840; negative ids are skipped). A sentry has -1, so the
  other PCs kept a copy that went on firing. Each Remote Turret's prop now gets
  0x7FFF0000 + the owner bits on every PC. For a sentry the receiver sets
  `OBJFLAG_DEACTIVATED` (flags |= 0x10000000) instead of deleting it, so each PC deletes
  another PC's deactivated turret itself. A turret shot to pieces is deactivated too:
  control ends without an explosion.
- **Picking up one's own turret** (vanilla Laptop behaviour) gives the Remote Turret
  back: sub_822211E8, after each `li r3,14` (0x822214B4, 0x822214C8).
- **Turret kills** credit the owner. The rocket explosion is large: in one test it also
  killed its owner.
- **Model** (`tools/models/make_turretgun.py`): the Laptop Gun's first-person file
  0x4C7 links mesh 0xA22 (texture 0x1591); the third-person file 0x358 (model 0x105)
  links mesh 0x855 (texture 0x0FC4). The new parts are added in Python. Mesh
  conventions: texture v runs up, and triangles are clockwise seen from the front.
  Borrowed model 0x19F has scale 4096, so `pd::custom::Install` now copies the replaced
  model's scale (409).

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

## Test tools added (branch weapon-research)

The pipe commands `give`, `equip`, `look`, `face <player|gamertag> [verta]`,
`tp`, `tp near`, `pos`, `track` and `watchplayer` (`src/pd_player.h`) are
described in `tools/test-scripts/README.md`. The research scenarios are
`research-tools`, `-hits`, `-ricochet`, `-ricochet2`, `-turret`, `-push`,
`-sentry`, `-tint` and `-handson`. They and the probes they switch on are in the
debug bank (`tools/debug-bank/weapon-research-scenarios/`,
`tools/debug-bank/weapon-research.cpp`); to run one again, restore the probes and
copy its folder back to `tools/test-scripts/`.
