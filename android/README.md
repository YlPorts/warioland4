# Wario Land 4 — Android ARM64 native playable alpha 0.9

**Scope: a playable exploration slice of the entry passage, not the complete
Wario Land 4 game.** You can control animated Wario in the original rooms,
walk, run, jump, land, crouch, shoulder bash, ground pound, climb basic
ladders, break ordinary blocks, and move through original room entrances.
There is no GBA CPU interpreter or emulator.

## Controls

| Input | Action |
| --- | --- |
| D-pad | Move / crouch / climb |
| A | Variable-height jump |
| B | Shoulder bash |
| L or R held | Run |
| Down pressed in midair | Ground pound |
| Up near a door, Up/Down at a pipe | Room entrance |
| START | Pause / resume |
| SELECT | Respawn at the current entrance |

Side exits follow the original entrance table automatically. The camera
follows Wario within the room. Broken blocks persist across room transitions
in the current session. SELECT is a recovery action for this alpha.

## What is original and what is adapted

- Room headers, stage and door records, tilesets, maps, Wario animation frame
  records, OAM entries, sprite graphics, palettes and hitboxes are read from
  the imported ROM using their original 32-bit GBA layouts.
- The original `DecompressRoomBackground()` and solid/slope/switch block
  kernels in `src/block.c` compile as native C. Their host wrappers replace
  platform headers and supply bounded game state; GBA branches are preserved.
- Wario artwork uses the original 2D OBJ row layout, including the second
  graphics upload at `0x06010400`. Source OAM offsets and flips are preserved.
- Player movement is an explicit portable subset implemented in
  `native_game.cpp`, using original position/velocity units and tuning from
  `src/wario/normal.c`: normal speed 64, run 128, bash 96, jump 176, crouch jump
  128, gravity 8 and fall limit 128. This does **not** execute the entire
  original 8,725-line pose machine, its collision resolution, or main loop.
- The subset uses swept collision against the real physical metatile table,
  six original slope kernels, one-way surfaces and a subset of block actions.
  Its behavior is not yet certified as matching the original game exactly.
- Texture storage is allocated once per GL context and reused for frame
  uploads rather than being reallocated every frame.

## Validation

CI uses an entirely hand-authored ROM-layout fixture, with no proprietary
bytes, to test gravity, jumps, landing, movement, pause, wall sweeps, block
break persistence and actual runtime room transitions. Both the original C
kernels and the native code run under AddressSanitizer/UBSan.

Local tests with the user's US-layout modified ROM
(`22b8707268a6c9c15cb9b8d2ec09754d`) checked the original Wario animations,
standing, jump, landing, horizontal movement, pause and every one of the
**29 entry records across the 13 entry-passage rooms**. The room loader still
validates 242 rooms from the full game's room tables, but this alpha starts
only the entry passage. A software render of the animated Wario scene was
visually inspected. Local LeakSanitizer was disabled due to this environment's
process inspection restriction; AddressSanitizer/UBSan remained enabled.

These are host logic/rendering tests and an Android ARM64 build, not an
Android device playthrough. No Samsung A15 performance result is claimed.

## ROM import

Accepted MD5 hashes:

- Stock US/EU: `5fe47355a33e3fabec2a1607af88a404`
- Stock JP: `99c8ad779a16be513a9fdff502b6f5c2`
- User's known US-layout modified ROM:
  `22b8707268a6c9c15cb9b8d2ec09754d`

The modified ROM is accepted in 0.9; the stock layouts are accepted but have
not been locally exercised in this session. No ROM or extracted proprietary
asset bytes are included in the repository, test fixture, APK or CI.

## Remaining work

Enemies, collectible sprites, damage/health, Keyzer, switches and escape
sequence progression, transformations, the full original pose/camera/game
state machines, original menus and cutscenes, audio, gameplay saves, remaining
video effects, widescreen and stable production signing remain incomplete.
Consequently, original objectives and the complete game cannot yet be played
through. Some room interactions need SELECT recovery until their original
handlers are ported.

Open `android/` in Android Studio, or download the explicitly labeled
`playable-alpha-entry-passage` development APK from the `android-native`
workflow. The workflow runs portable regression tests and builds ARM64.
