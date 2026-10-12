# Wario Land 4 — Android native port (work in progress)

This branch is the **initial Android platform milestone**, not a playable port.

## What works (code milestone)
- Android ARM64 application project (Java launcher + JNI/C++17 + OpenGL ES 2.0).
- Android Storage Access Framework ROM picker on first launch.
- ROM checksum validation: original Wario Land 4 USA/EU or JP, required from the user.
- Reads uncompressed common sprite tiles/palette from the ROM and displays a native 4bpp tile atlas with nearest-neighbour scaling.
- Multi-touch GBA button input collection, not yet connected to game logic.
- Standalone native tile-decoder test.

## What DOES NOT work yet
- **No game execution.** The original game loop and logic are NOT linked.
- GBA register/VRAM/OAM compatibility layer, background/sprite renderer and DMA.
- VBlank/HBlank scheduling, audio/M4A, SRAM persistence.
- Wide-screen camera, polished UI, and release signing.

The original decompilation's Makefile builds a GBA ROM with agbcc and ARM7TDMI
hardware addresses. Its output is not a PC/Android executable. A native port
must compile the game logic for Android and replace GBA platform dependencies;
CPU emulation is NOT the goal of this branch.

## Build

Open the `android/` directory with Android Studio, using JDK 17+, API 36
and NDK 27.0.12077973. Android Gradle Plugin 8.13.2 / Gradle 8.13.

To build with the command-line Gradle installation:

```sh
gradle -p android :app:assembleDebug
```

To test the portable ROM tile decoder on Linux:

```sh
c++ -std=c++17 -Wall -Wextra -Werror \
  android/app/src/main/cpp/rom_assets.cpp \
  android/tests/test_rom_assets.cpp -o /tmp/wl4-assets-test
/tmp/wl4-assets-test
```

No commercial game files are committed or distributed. Users need their own
original, hash-matched Wario Land 4 ROM. The native source here is preparatory;
a successfully built APK is **not evidence** that the game runs.

## Next technical milestone

Port the GBA video/register interface (VRAM, palettes, tilemaps, OAM and frame
callbacks) behind Android-native buffers/rendering, then replace the original
game's GBA-only routines one by one. Preserve original game timing, and add
ultrawide only after playable 240x160 rendering is validated.
