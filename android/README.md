# Wario Land 4 — Android native port (in development)

## Current milestone: 0.2 video diagnostic (NOT playable)

The Android ARM64 app and native GLES2 surface are working as a build target.
The ROM picker accepts original MD5-matched Wario Land 4 USA/EU or Japan ROMs.
With a ROM imported the application shows a **diagnostic** tilemap and one
sprite made from genuine ROM tile data, not an actual game level.
The D-pad scrolls the diagnostic tilemap and background layer.

New host-native \`VideoState\` / \`GbaVideo\` renderer covers:
- A separate VRAM (96 KiB), palette (1 KiB), OAM (1 KiB) snapshot.
- Tiled video mode 0, four scrollable BG layers, BG 4bpp/8bpp palette decoding.
- 32x32, 64x32, 32x64 and 64x64-tile maps, horizontal/vertical tile flips.
- Regular OBJ sprites in 1D/2D tile layout (4bpp and 8bpp), sprite flips,
  transparency, OAM index and BG/OBJ priority ordering.
- Forced blank and backdrop color.
- A native fixed-step clock using the GBA 59.7275 Hz video rate, independent
  of a phone's 60/90/120 Hz display. Android requests rendering at vsync.

**IMPORTANT:** This is a video platform backend, not a CPU emulator, and it is
**not yet connected to the decompiled Wario Land 4 game loop**. Audio, save data,
VBlank callbacks, DMA, bitmap/affine video modes, blending, windows, mosaic,
affine sprites, game-specific camera and full gameplay are still missing.
The renderer's diagnostic scene is NOT a playable game or a gameplay preview.
Do not distribute it as if it were a finished native port.

## Build and test

Android Studio: open \`android/\`, install Android SDK 36 and NDK 27.
GitHub Actions on branch \`android-native\` runs host-unit tests and builds a
debug ARM64 APK, uploaded as a clearly labeled non-playable milestone.

\`\`\`sh
c++ -std=c++17 -Wall -Wextra -Werror \
  android/app/src/main/cpp/rom_assets.cpp \
  android/tests/test_rom_assets.cpp -o /tmp/wl4-assets-test
/tmp/wl4-assets-test

c++ -std=c++17 -Wall -Wextra -Werror \
  android/app/src/main/cpp/gba_video.cpp \
  android/app/src/main/cpp/frame_clock.cpp \
  android/tests/test_gba_video.cpp -o /tmp/wl4-video-test
/tmp/wl4-video-test

gradle -p android --no-daemon :app:assembleDebug
\`\`\`

Next major step: compile actual C game logic for the Android host and replace
its raw GBA hardware register references/interrupts with typed platform APIs.
The decompilation's existing make target still reconstructs a GBA ROM.
An original ROM is required only as an asset source in the Android app.
