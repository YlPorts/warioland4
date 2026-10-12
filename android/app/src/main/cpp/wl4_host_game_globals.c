#include "wl4_host_game.h"

volatile u16 wl4HostKeyInput = 0xffff;
volatile u16 wl4HostIrqFlags = 0;
vu16 gInterruptCheck = 0;
struct ScreenShakeParameters gScreenShakeX = {0};
struct ScreenShakeParameters gScreenShakeY = {0};
ProcedureFunc gVBlankCallback = 0;
ProcedureFunc gHBlankCallback = 0;
ProcedureFunc gVCountCallback = 0;
