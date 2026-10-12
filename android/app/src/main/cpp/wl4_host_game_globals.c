#include "wl4_host_game.h"
u8 wl4HostBlockDamageTimer = 0;
u8 gSwitchStates[5] = {0};

volatile u16 wl4HostKeyInput = 0xffff;
volatile u16 wl4HostIrqFlags = 0;
vu16 gInterruptCheck = 0;
struct ScreenShakeParameters gScreenShakeX = {0};
struct ScreenShakeParameters gScreenShakeY = {0};
ProcedureFunc gVBlankCallback = 0;
ProcedureFunc gHBlankCallback = 0;
ProcedureFunc gVCountCallback = 0;

static void *wl4HostDmaContext = 0;
static Wl4HostDmaFillHandler wl4HostDmaCallback = 0;

void wl4HostSetDmaFillHandler(void *context, Wl4HostDmaFillHandler callback)
{
    wl4HostDmaContext = context;
    wl4HostDmaCallback = callback;
}

void wl4HostDmaFill16(unsigned int channel, uint32_t pattern,
                      uint32_t address, uint32_t bytes)
{
    if (channel == 3 && wl4HostDmaCallback != 0)
        (void)wl4HostDmaCallback(wl4HostDmaContext, address, pattern, bytes, 2);
}

void wl4HostDmaFill32(unsigned int channel, uint32_t pattern,
                      uint32_t address, uint32_t bytes)
{
    if (channel == 3 && wl4HostDmaCallback != 0)
        (void)wl4HostDmaCallback(wl4HostDmaContext, address, pattern, bytes, 4);
}
