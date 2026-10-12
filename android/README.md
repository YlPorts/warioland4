# Wario Land 4 — Android ARM64 native port, milestone 0.7

**Current state: NOT PLAYABLE.** This still displays a diagnostic tile
scene based on original ROM graphics. It does not yet load or execute actual
rooms, Wario physics, enemies, audio or the original main game loop.

## New in 0.7

- The **real `InitializeVideoMemory()` function from `src/init_helpers.c`**
  now runs on the Android ARM64 build. This is the original game's routine,
  not a reimplementation.
- The original routine's DMA fill macros are redirected to a typed callback
  backed by `NativeMemory::FillRegion()`, with checked 16-bit and 32-bit
  repeated patterns. Video initialization now clears VRAM, palette, OAM.
- `NativeMemory::CopyRegion()` moves actual asset bytes from the
  original imported ROM into BG/OBJ VRAM and palettes. Diagnostic graphics
  now use these native DMA-like transfers rather than directly copying
  extracted arrays into a prebuilt video snapshot.
- Copy/fill operations enforce alignment, size, physical bank boundaries,
  legal writable memory regions and reject invalid transfers before writing.
  Overlapping copies use an atomic source snapshot.
- CI exercises the original video initialization end-to-end, verifies
  exact clear patterns and tests invalid/overlapping transfers.

## Previously integrated

Native GLES2 video renderer for tiled BG and OBJ sprites plus bitmap modes
3/4/5; native GBA-style memory, ROM picker with ROM hash verification,
safe LZ77/RLE decoder, frame clock, SRAM storage infrastructure.
Original C modules compiled for ARM64: `fixed_point.c`, `PollInput`,
screen shaking, VBlank/HBlank/VCount callbacks, `CheckSoftReset`, now
`InitializeVideoMemory`. These functions are genuine decompiled code.

## Still required to boot the real game

1. Port ROM level asset addresses, BIOS/DMA callers and raw GBA pointers to
   safe host memory or native game-state structures.
2. Port `InitializeGame`, `AgbMain`'s game-mode dispatch, cutscenes,
   menus, camera, collision, level loading, sprites and Wario movement.
3. Translate sound/M4A, game saves, additional graphics features and all
   platform IRQ semantics.
4. Validate gameplay on a real Android device; only then optimize
   widescreen, smooth frame pacing and production APK signing.

Open `android/` in Android Studio, or download the labeled *not-playable*
development APK from the `android-native` GitHub Actions workflow.
No proprietary ROM assets are included in the APK; a user-owned original
US/EU or Japan ROM must be provided.
