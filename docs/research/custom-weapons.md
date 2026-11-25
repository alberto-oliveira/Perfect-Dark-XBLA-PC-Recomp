# Custom weapons research

Research for four planned custom weapons, in priority order: **Ricochet Laser >
Remote Turret > Push Gun > Slow Gun**. Nothing here is implemented: this
document records what each weapon needs, what the game already does online,
and which hooks a weapon's implementation will use.

How the answers were found:

- **Statically:** from the game's code, using `tools/re` and the N64 decomp.
- **At runtime:** from test rounds T0–T7 on 2–3 game copies, driven by the test
  pipe scenarios `research-*` in `tools/test-scripts/`.
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
| Bounce tracer | Crossbow-bolt trail pool `g_BoltBeams` 0x8262E958 (8 × 48 B: +0 owner, -1 = free; +5 tick mode, 1 = automatic; +20 head; +32 tail; +44 speed), drawn by `boltbeams_render` sub_821844F0 with the laser texture; a slot set to automatic shrinks and frees itself (verified in code). **Not yet seen on screen**: the probe claims a slot (logged), but no screenshot caught a visible tracer (T3, T4, T8). Check by eye with `research-handson`; if it doesn't show, a beam from the multiplayer fireslot pool (`g_Fireslots`, N64 bondgun.c `bgun_allocate_fireslot`) is the alternative. |
| Colour | `beam_render` sub_82182A30: RGBA written at 0x82182BB4 (r25), branches join at 0x82182C90 (r29 = beam, weapon at +1: 29 Laser, -2 Stream); setting the RGB there **turns the Laser beam green** (verified, T8 screenshot of the Stream). The bolt trail's colours are r7/r10 at 0x82184554 (0xAFAFFF00/0xAFAFFF7F) (verified in code). Every PC draws every player's beams through it, so a tint shows everywhere. |
| Stream | Same path, range 300 (constant 300.0 at 0x82000B08), many shots per second: each needs its own bounce test. |

### Implementation outline

1. **Registry entry.** Add the Ricochet Laser to `pd_custom_weapons.h` (base 29,
   a borrowed number such as 0x52). `AsBase` at the Laser checks in
   `beam_render` and in `shot_calculate_hits` (the `laserstream` test).
2. **Settings (host blob, like 'XBOW'):** bounce count, damage growth, kill-after-N.
3. **Hook at 0x821FC9B8, for every PC.** If the shot is this weapon's:
   - walk up to N bounces with the line test;
   - draw each segment with a bolt-trail slot;
   - only on the shooter's PC (players[0] fired), test each segment against
     every player and apply `chr_damage_by_laser` with the bounce's damage;
   - the first segment is the game's own shot, so let the game damage it.
4. **Tint:** hook 0x82182C90 (beam) and 0x82184554 (bounce trails), keyed by
   the custom weapon number.

**Feasibility: high. Difficulty: ★★★☆☆** (lower than estimated: no network
code is needed).

**Tests it will need:**

- 2–3 copies: bounces visible on every PC;
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
| Steering | Overriding the goals at 0x822078E8 and firing at 0x82207B08 on the owner: the owner's sentry turned and fired where told, **and the other PC's copy followed** (verified, T7). Goal 88(r1) lands in the +120 field, 84(r1) in +108. Skipping `autogun_tick` instead (T6) turned only the owner's copy. |
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

## Test tools added (branch weapon-research)

The pipe commands `give`, `equip`, `look`, `face <player|gamertag> [verta]`,
`tp`, `tp near`, `pos`, `track` and `watchplayer` (`src/pd_player.h`) are
described in `tools/test-scripts/README.md`. The research scenarios are
`research-tools`, `-hits`, `-ricochet`, `-ricochet2`, `-turret`, `-push`,
`-sentry`, `-tint` and `-handson`. The probes they switch on are in the debug
bank.
