#include "../app/src/main/cpp/wl4_host_game.h"
#include <cassert>
#include <iostream>

static int vblankCount = 0;
static int hblankCount = 0;
static int vcountCount = 0;
extern "C" void testVblank() { ++vblankCount; }
extern "C" void testHblank() { ++hblankCount; }
extern "C" void testVcount() { ++vcountCount; }

static void testOriginalInput() {
    wl4HostKeyInput = 0xffff;
    gButtonsHeld = gButtonsHeldCopy = gButtonsPressed = 0;
    PollInput();
    assert(gButtonsPressed == 0);
    wl4HostKeyInput = static_cast<u16>(0xffffu & ~(0x1u | 0x10u));
    PollInput();
    assert(gButtonsHeld == (0x1u | 0x10u));
    assert(gButtonsPressed == (0x1u | 0x10u));
    PollInput();
    assert(gButtonsPressed == 0);
    wl4HostKeyInput = 0xffff;
    PollInput();
    assert(gButtonsHeld == 0 && gButtonsPressed == 0);
    wl4HostKeyInput = 0xfffe;
    PollInput();
    assert(gButtonsPressed == 1);
}
static void testOriginalShake() {
    gScreenShakeX = {};
    gScreenShakeY = {};
    assert(ScreenShakeRequestXWithAmplitude(20, 3) == 20);
    assert(ScreenShakeRequestXWithAmplitude(2, 2) == 20);
    assert(ScreenShakeUpdateX() == 0);
    assert(ScreenShakeUpdateX() == 0);
    assert(ScreenShakeUpdateX() == -2);
    assert(ScreenShakeUpdateX() == 0);
    assert(ScreenShakeUpdateX() == 0);
    assert(ScreenShakeUpdateX() == 2);
    assert(ScreenShakeRequestY(10, 1) == 10);
    assert(ScreenShakeUpdateY() == 0);
    assert(ScreenShakeUpdateY() == 0);
    assert(ScreenShakeUpdateY() == -1);
}
static void testOriginalInterruptCallbacks() {
    gInterruptCheck = 0;
    wl4HostIrqFlags = 0;
    InterruptCallbackSetVBlank(testVblank);
    InterruptCallbackSetHBlank(testHblank);
    InterruptCallbackSetVCount(testVcount);
    InterruptCallbackCallVBlank();
    InterruptCallbackCallHBlank();
    InterruptCallbackCallVCount();
    assert(vblankCount == 1 && hblankCount == 1 && vcountCount == 1);
    assert((wl4HostIrqFlags & 7u) == 7u);
    assert(gInterruptCheck == 1);
    InterruptCallbackSetVBlank(nullptr);
    InterruptCallbackCallVBlank();
    assert(vblankCount == 1);
}
int main() {
    testOriginalInput();
    testOriginalShake();
    testOriginalInterruptCallbacks();
    std::cout << "Original input/shake/IRQ callbacks OK\n";
}
