# Wario Land 4 — Android ARM64 native port, milestone 0.8

**Current state: NOT PLAYABLE.** The Android app now previews original room
backgrounds from an imported ROM. Wario movement, collision, enemies,
menus, audio and the original main game loop are not connected yet.

## New in 0.8

- The original `DecompressRoomBackground()` function in
  `src/game_screen_helpers.c` compiles as native C on ARM64. Only its ARM7
  register binding is removed for the host build. The GBA branch retains the
  original function and all other routines.
- A bounds validator checks dimensions, both RLE byte planes, literal and
  repeat runs, source truncation and output capacity before calling that
  original decoder. Invalid inputs leave the previous output unchanged.
- `RoomAssets` reads the original 44-byte room headers and 36-byte tileset
  descriptors field-by-field. Raw 32-bit GBA pointers never become host
  pointers or 64-bit struct casts.
- The loader follows the original stage/room tables, imports room tiles,
  palettes, animated graphics, metatile maps and collision attributes,
  and expands visible metatiles into the original screen block layout.
- Native preview starts near the original room entrance. The D-pad pans a
  bounded camera; L/R loads the previous/next room in the entry passage.
  These are inspection controls, not Wario movement or gameplay.
- Tile flips, palette banks, layer priorities and basic BG3 parallax are
  preserved by the existing host video renderer. Windows, blending,
  background effects, dynamic tile changes and animated graphics playback
  still need integration; imported animation graphics are initially static.

## Validation

Portable unit tests cover both RLE run widths, all valid decoder types,
truncation, output overrun, incomplete planes and non-mirrored ROM bounds.
The local optional ROM test loaded **242 rooms with zero rejections** and
rendered the first entry-passage room at 656×368 world pixels. This ran with
AddressSanitizer and UndefinedBehaviorSanitizer; leak detection was disabled
because this execution environment prevents LeakSanitizer process inspection.

The supplied test ROM has US-layout code `AWAE` and MD5
`22b8707268a6c9c15cb9b8d2ec09754d`: it is modified, not a verified stock ROM.
This validates that file's room tables, not the canonical US/JP ROMs or device
performance. The APK still requires the verified original US/EU or JP ROM
hashes from the repository. No ROM or proprietary extracted assets are
committed, included in the APK, or uploaded to CI.

CI runs portable regression tests, the new sanitized room decoder tests and
an ARM64 Android build. No Samsung A15 hardware test has been performed.

Optional local ROM validation (after building `wl4-room-test` using the CI
compiler command):

```sh
ASAN_OPTIONS=detect_leaks=0 /tmp/wl4-room-test /path/to/owned-rom.gba /tmp/first-room.ppm
```

## Previously integrated

Original native C modules: fixed-point arithmetic, input, soft reset,
screen shaking, IRQ callbacks and `InitializeVideoMemory()` via checked
16/32-bit DMA fills. Host memory, SRAM persistence infrastructure, safe BIOS
LZ77/RLE decoding, GLES2 presentation and logical frame pacing remain in place.

## Still required to boot the real game

1. Port game-state globals, original room initialization, camera and
   collision routines to safe native structures.
2. Connect original Wario physics, sprite animation, enemies, tile
   interactions and the main game-mode dispatch.
3. Port cutscenes, menus, M4A sound, game saves, platform callbacks and
   remaining graphics effects.
4. Validate real gameplay on Android, then optimize widescreen and frame
   pacing and configure stable production APK signing.

Open `android/` in Android Studio, or use the labeled *not-playable* artifact
from the `android-native` GitHub Actions workflow. This is a native C/C++ port;
no GBA CPU interpreter or emulator is included.
