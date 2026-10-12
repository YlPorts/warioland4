#include "../app/src/main/cpp/wl4_host_game.h"
#include <cassert>
#include <iostream>
int main() {
    gMainGameMode = GM_CUTSCENE;
    gDisableSoftReset = 0;
    gButtonsHeld = A_BUTTON | B_BUTTON | START_BUTTON;
    CheckSoftReset();
    assert(gMainGameMode == GM_CUTSCENE);
    gButtonsHeld |= SELECT_BUTTON;
    CheckSoftReset();
    assert(gMainGameMode == GM_SOFT_RESET);
    gMainGameMode = GM_GAME_SCREEN;
    gDisableSoftReset = 1;
    CheckSoftReset();
    assert(gMainGameMode == GM_GAME_SCREEN);
    gDisableSoftReset = 0;
    CheckSoftReset();
    assert(gMainGameMode == GM_SOFT_RESET);
    std::cout << "Original main.c CheckSoftReset ARM64 host test OK\n";
}
