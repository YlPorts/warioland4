# Wario Land 4 — Android ARM64 native port (in development)

## Milestone 0.3: native platform memory + first original C module

**Not a playable Wario Land 4 build.** This is a host-native port
foundation, without ARM7 CPU emulation and without an Android game loop yet.

Integrated:
- Original `src/fixed_point.c` from the decompilation compiled directly
  into the Android ARM64 shared library. The camera **diagnostic**, not game
  camera, now calls the original `FixedMul`.
- Native address-space interface (`NativeMemory`) for EWRAM (256 KiB),
  IWRAM (32 KiB), GBA video registers, VRAM (96 KiB), palette (1 KiB),
  OAM (1 KiB), read-only ROM windows and battery SRAM (64 KiB).
- GBA active-low input register `KEYINPUT` backed by Android touch buttons.
- Disk-backed SRAM read/write with temporary-file replacement. The
  game's saving routines are NOT connected yet.
- The pre-existing host-native diagnostic renderer captures all graphics
  state from the new register/VRAM/OAM memory interface.
- ROM picker requires a user-owned hash-verified original USA/EU or JP ROM.
- OpenGL ES 2.0 presentation, GBA-rate frame clock, mode 0 BG + basic OBJ.

Tests: memory bus, ROM window mapping, KEYINPUT, register/video snapshots,
SRAM persistence, original fixed-point C arithmetic, pixel-level BG/OBJ
rendering, original ROM asset tile decoding; plus an Android ARM64 APK build.

**Still missing before gameplay:** many decompiled modules still use
ARM-specific register allocations/assembly, 32-bit GBA absolute addresses,
DMA, sound hardware and IRQ callbacks. Wario physics, level logic, actual
save data and scene transitions are not linked. This branch is not an
emulator, and the 0.3 APK only shows a ROM-resource diagnostic tile scene.

Next: systematically migrate source modules and hardware accesses into the
typed host-memory interface, then initialize the game state machine at the
original 240×160 resolution. Only then validate ultrawide, audio and 60fps.

## Local build

Open `android/` in Android Studio or run
`gradle -p android --no-daemon :app:assembleDebug`.
Continuous integration is in `.github/workflows/android-native.yml`.
