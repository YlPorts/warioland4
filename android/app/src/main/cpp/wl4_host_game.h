#ifndef WL4_HOST_GAME_H
#define WL4_HOST_GAME_H
// Compile selected original GBA C routines on ARM64 without CPU emulation.
// Only their platform-dependent headers/globals are substituted.
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef volatile u16 vu16;
typedef void (*ProcedureFunc)(void);
#define S8_MAX 127
#define DELTA_TIME 1
#define CONVERT_SECONDS(s) ((u32)((s) * 60))
#ifndef KEYS_MASK
#define KEYS_MASK 0x03ffu
#endif

extern volatile u16 wl4HostKeyInput;
extern volatile u16 wl4HostIrqFlags;
#ifndef REG_KEYINPUT
#define REG_KEYINPUT (wl4HostKeyInput)
#endif
#ifndef REG_IF
#define REG_IF (wl4HostIrqFlags)
#endif
#ifndef INTR_FLAG_VBLANK
#define INTR_FLAG_VBLANK 1u
#endif
#ifndef INTR_FLAG_HBLANK
#define INTR_FLAG_HBLANK 2u
#endif
#ifndef INTR_FLAG_VCOUNT
#define INTR_FLAG_VCOUNT 4u
#endif

extern u16 gButtonsHeld;
extern u16 gButtonsHeldCopy;
extern u16 gButtonsPressed;
void PollInput(void);

// Original InitializeVideoMemory uses these GBA DMA macros. The callbacks
// bridge its 16/32-bit fills to bounded NativeMemory instead of MMIO.
#define VRAM 0x06000000u
#define VRAM_SIZE 0x18000u
#define OAM 0x07000000u
#define OAM_SIZE 0x400u
#define PLTT 0x05000000u
#define PLTT_SIZE 0x400u
#define U8_MAX 255u
typedef int (*Wl4HostDmaFillHandler)(void *context, uint32_t address,
                                    uint32_t pattern, uint32_t bytes,
                                    uint32_t unitBytes);
void wl4HostSetDmaFillHandler(void *context, Wl4HostDmaFillHandler callback);
void wl4HostDmaFill16(unsigned int channel, uint32_t pattern, uint32_t address, uint32_t bytes);
void wl4HostDmaFill32(unsigned int channel, uint32_t pattern, uint32_t address, uint32_t bytes);
#define DmaFill16(channel, pattern, address, bytes) \
    wl4HostDmaFill16((channel), (pattern), (address), (bytes))
#define DmaFill32(channel, pattern, address, bytes) \
    wl4HostDmaFill32((channel), (pattern), (address), (bytes))
void InitializeVideoMemory(void);
u32 GetBackgroundScreenSize(u8 parameter);
u32 DecompressRoomBackground(u8 type, const u8* source, u8* destination);

// Wario Land 4's original main.c mode values and soft-reset button handler.
enum {
    GM_CUTSCENE = 0, GM_STAGE_SELECT = 1, GM_GAME_SCREEN = 2,
    GM_SOFT_RESET = 3, GM_PAUSE = 4
};
#define A_BUTTON 0x0001u
#define B_BUTTON 0x0002u
#define SELECT_BUTTON 0x0004u
#define START_BUTTON 0x0008u
#define CHECK_KEYS_ALL(keys,mask) (((keys) & (mask)) == (mask))
extern s16 gMainGameMode;
extern u8 gDisableSoftReset;
void CheckSoftReset(void);

struct ScreenShakeParameters {
    u8 duration;
    u8 frameTimer;
    u8 amplitude;
    u8 direction;
};
extern struct ScreenShakeParameters gScreenShakeX;
extern struct ScreenShakeParameters gScreenShakeY;
u8 ScreenShakeRequestY(u8 duration, u8 amplitude);
u8 ScreenShakeRequestX(u8 duration);
u8 ScreenShakeRequestXWithAmplitude(u8 duration, u8 amplitude);
s32 ScreenShakeUpdateX(void);
s32 ScreenShakeUpdateY(void);

extern vu16 gInterruptCheck;
extern ProcedureFunc gVBlankCallback;
extern ProcedureFunc gHBlankCallback;
extern ProcedureFunc gVCountCallback;
void InterruptCallbackCallVBlank(void);
void InterruptCallbackCallHBlank(void);
void InterruptCallbackCallVCount(void);
void InterruptCallbackSetVBlank(ProcedureFunc cb);
void InterruptCallbackSetHBlank(ProcedureFunc cb);
void InterruptCallbackSetVCount(ProcedureFunc cb);
void InterruptCallbackEmpty(void);
#ifdef __cplusplus
}
#endif
#endif
