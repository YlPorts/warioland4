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
