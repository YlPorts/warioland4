# Wario Land 4 Android — ARM64 port, milestone 0.4

**NOT PLAYABLE.** This app currently renders an original-asset tile diagnostic scene.
It is not executing original levels, Wario physics or the game state machine.

## Actual decompiled game C now linked into the Android native .so

- `src/fixed_point.c` — original arithmetic (already linked in v0.3).
- `src/init_helpers.c` — original `PollInput` with pressed-edge detection.
- `src/screen_shake.c` — original shake request/update behavior.
- `src/interrupt_callbacks.c` — original VBlank/HBlank/VCount dispatch.

`WL4_ANDROID_NATIVE` guards switch only platform-dependent includes and
unported GBA helper functions. Original implementations of the selected
functions remain unchanged and the normal GBA build retains its original path.

The Android diagnostic now runs the original input and shake functions once
per logical GBA tick, with A/B causing shake and D-pad scrolling the native
tilemap. VBlank is signaled through the decompiled callback dispatcher.
This **does not** mean the original game loop is operational.

The host memory backend, VRAM/OAM tile renderer, ROM importer, original
ROM checksum checks and SRAM storage scaffolding are unchanged from v0.3.

## Remaining work before actual gameplay

- Replace game logic's ARM7 inline asm and hardcoded GBA pointers.
- Safely translate DMA, decompression, interrupts and GBA BIOS services.
- Integrate the original `AgbMain` state machine at native frame ticks.
- Implement original level loading, collision/physics, OBJ effects, music,
  room transitions and proper SRAM game save compatibility.
- Validate on a physical Android device, then build ultrawide support.

No copyrighted ROM content is distributed. The CI debug APK is a
**non-playable development checkpoint**, not a release.
See `.github/workflows/android-native.yml` for build and tests.

## 0.5 — Original main.c handler linked

The Android ARM64 build now compiles `src/main.c` in a guarded host mode.
Only the original `CheckSoftReset` function, `gMainGameMode`, and
`gDisableSoftReset` are enabled so far. A+B+Start+Select exercises
the original handler. The full `AgbMain` loop and other game systems
remain GBA-only. This is an incremental native game-code milestone
and **does not run levels**.
