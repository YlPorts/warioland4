#include <GLES2/gl2.h>
#include <jni.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <vector>

#include "frame_clock.h"
#include "gba_video.h"
#include "native_memory.h"
#include <string>

extern "C" {
#include "fixed_point.h"
#include "wl4_host_game.h"
}

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "wl4-native", __VA_ARGS__)

namespace {
constexpr size_t kPaletteOffset = 0x400A68;  // common_sprites.pal (US/JP)
constexpr size_t kTileOffset = 0x400AE8;     // common_sprites.4bpp (US/JP)
constexpr size_t kTileBytes = 0x3000;
constexpr size_t kTileCount = kTileBytes / 32;
std::mutex gMutex;
wl4::NativeMemory gMemory;
std::string gSavePath;
wl4::VideoState gState;
wl4::GbaVideo gVideo;
wl4::FrameClock gClock;
std::vector<uint32_t> gPixels;
bool gSceneLoaded = false;
bool gUploadPending = false;
uint16_t gCameraBaseX = 0;
uint16_t gCameraBaseY = 0;

GLuint gTexture = 0;
GLuint gProgram = 0;
GLint gPos = -1, gUV = -1, gSampler = -1;
int gWidth = 1, gHeight = 1;

void write16(uint8_t* p, size_t at, uint16_t val) {
    p[at] = static_cast<uint8_t>(val & 255u);
    p[at + 1] = static_cast<uint8_t>(val >> 8);
}

// A diagnostic scene built from original tiles. This is NOT the game engine;
// it checks palette/VRAM/OAM/camera functionality while porting the C logic.
void buildDiagnosticScene(const std::vector<uint8_t>& rom) {
    wl4::VideoState state;
    std::copy_n(rom.begin() + kPaletteOffset, 128, state.palette.begin());
    std::copy_n(rom.begin() + kPaletteOffset, 128, state.palette.begin() + 0x200);
    std::copy_n(rom.begin() + kTileOffset, kTileBytes, state.vram.begin());
    std::copy_n(rom.begin() + kTileOffset, kTileBytes, state.vram.begin() + 0x10000);

    // Use tiled mode 0, 1D sprite mapping, BG0/BG1 + OBJ.
    state.dispcnt = 0x40 | 0x100 | 0x200 | 0x1000;
    state.bgcnt[0] = 16u << 8;       // screen block 16, priority 0
    state.bgcnt[1] = (17u << 8) | 1; // screen block 17, priority 1

    // Both tilemaps use decoded original sprite data as a render diagnostic.
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const size_t tile = static_cast<size_t>(y * 32 + x);
            write16(state.vram.data(), 0x8000 + tile * 2,
                    static_cast<uint16_t>(1 + tile % (kTileCount - 1)));
            write16(state.vram.data(), 0x8800 + tile * 2,
                    static_cast<uint16_t>((tile * 7) % kTileCount));
        }
    }
    for (int i = 0; i < 128; ++i) {
        write16(state.oam.data(), static_cast<size_t>(i) * 8, 0x0200);
    }
    // One 32x32 sprite to validate priority and compositing. Not a playable Wario.
    write16(state.oam.data(), 0, 64);          // sprite Y
    write16(state.oam.data(), 2, 104 | 0x8000); // size=2 (32x32)
    write16(state.oam.data(), 4, 32 | 0x0000);  // tile 32, priority 0
    gCameraBaseX = gCameraBaseY = 0;
    gScreenShakeX = {};
    gScreenShakeY = {};
    gButtonsHeld = gButtonsHeldCopy = gButtonsPressed = 0;
    gInterruptCheck = 0;
    wl4HostIrqFlags = 0;
    InterruptCallbackSetVBlank(nullptr);
    gMemory.InstallVideoState(state);
    gState = gMemory.CaptureVideo();
    gVideo.Render(gState, &gPixels);
    gSceneLoaded = true;
    gUploadPending = true;
    gClock.Reset();
}

GLuint shader(GLenum type, const char* code) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &code, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char msg[512] = {};
        glGetShaderInfoLog(s, sizeof(msg), nullptr, msg);
        LOGE("GL shader compilation failed: %s", msg);
        glDeleteShader(s);
        return 0;
    }
    return s;
}
void makeProgram() {
    static const char* vertex =
        "attribute vec2 aPos; attribute vec2 aUV;"
        "varying vec2 vUV;"
        "void main() { gl_Position=vec4(aPos,0.0,1.0); vUV=aUV; }";
    static const char* fragment =
        "precision mediump float; varying vec2 vUV; uniform sampler2D uTex;"
        "void main() { gl_FragColor=texture2D(uTex,vUV); }";
    GLuint vs = shader(GL_VERTEX_SHADER, vertex);
    GLuint fs = shader(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return;
    }
    gProgram = glCreateProgram();
    glAttachShader(gProgram, vs);
    glAttachShader(gProgram, fs);
    glLinkProgram(gProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(gProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("GL program link failed");
        glDeleteProgram(gProgram);
        gProgram = 0;
        return;
    }
    gPos = glGetAttribLocation(gProgram, "aPos");
    gUV = glGetAttribLocation(gProgram, "aUV");
    gSampler = glGetUniformLocation(gProgram, "uTex");
}

void uploadTexture() {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gUploadPending || gPixels.empty()) return;
    if (!gTexture) glGenTextures(1, &gTexture);
    glBindTexture(GL_TEXTURE_2D, gTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wl4::VideoState::Width,
                 wl4::VideoState::Height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 gPixels.data());
    gUploadPending = false;
}

void updateDiagnosticCamera(int ticks) {
    if (!gSceneLoaded) return;
    bool redraw = false;
    // Call the original decompiled input, shake and interrupt handlers once
    // per logical frame instead of tying game ticks to the Android refresh rate.
    for (int frame = 0; frame < ticks; ++frame) {
        wl4HostKeyInput = gMemory.Read16(0x04000130);
        PollInput();
        if (gButtonsPressed & 1u) ScreenShakeRequestXWithAmplitude(24, 3);
        if (gButtonsPressed & 2u) ScreenShakeRequestY(24, 3);
        const int dx = ((gButtonsHeld & 16u) != 0) -
                       ((gButtonsHeld & 32u) != 0);
        const int dy = ((gButtonsHeld & 128u) != 0) -
                       ((gButtonsHeld & 64u) != 0);
        const int16_t step = Q_8_8(0.5);
        gCameraBaseX = static_cast<uint16_t>(gCameraBaseX + FixedMul(dx * 4, step));
        gCameraBaseY = static_cast<uint16_t>(gCameraBaseY + FixedMul(dy * 4, step));
        // The real screen-shake functions now drive only our diagnostic scene.
        const int shakeX = ScreenShakeUpdateX();
        const int shakeY = ScreenShakeUpdateY();
        const uint16_t x = static_cast<uint16_t>(gCameraBaseX + shakeX);
        const uint16_t y = static_cast<uint16_t>(gCameraBaseY + shakeY);
        if (x != gMemory.Read16(0x04000010) ||
            y != gMemory.Read16(0x04000012)) {
            gMemory.Write16(0x04000010, x);
            gMemory.Write16(0x04000012, y);
            gMemory.Write16(0x04000014, static_cast<uint16_t>(gCameraBaseX / 2 + shakeX / 2));
            gMemory.Write16(0x04000016, static_cast<uint16_t>(gCameraBaseY / 2 + shakeY / 2));
            redraw = true;
        }
        gInterruptCheck = 0;
        InterruptCallbackCallVBlank();
    }
    if (!redraw) return;
    gState = gMemory.CaptureVideo();
    gVideo.Render(gState, &gPixels);
    gUploadPending = true;
}
}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ylports_warioland4_MainActivity_nativeLoadRom(JNIEnv* env, jclass, jstring path) {
    if (!path) return JNI_FALSE;
    const char* name = env->GetStringUTFChars(path, nullptr);
    if (!name) return JNI_FALSE;
    std::ifstream in(name, std::ios::binary | std::ios::ate);
    env->ReleaseStringUTFChars(path, name);
    if (!in) return JNI_FALSE;
    const auto length = in.tellg();
    if (length < 0 || length > 32 * 1024 * 1024 ||
        length < static_cast<std::streamoff>(kTileOffset + kTileBytes)) {
        return JNI_FALSE;
    }
    in.seekg(0);
    std::vector<uint8_t> rom(static_cast<size_t>(length));
    if (!in.read(reinterpret_cast<char*>(rom.data()), length)) return JNI_FALSE;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (!gMemory.LoadRom(rom)) return JNI_FALSE;
        buildDiagnosticScene(rom);
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_warioland4_MainActivity_nativeSurfaceCreated(JNIEnv*, jclass) {
    gTexture = 0;
    gProgram = 0;
    makeProgram();
    std::lock_guard<std::mutex> lock(gMutex);
    gUploadPending = !gPixels.empty();
}

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_warioland4_MainActivity_nativeSurfaceChanged(JNIEnv*, jclass,
                                                                jint width, jint height) {
    gWidth = std::max(1, static_cast<int>(width));
    gHeight = std::max(1, static_cast<int>(height));
    glViewport(0, 0, gWidth, gHeight);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_warioland4_MainActivity_nativeResetClock(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(gMutex);
    gClock.Reset();
}

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_warioland4_MainActivity_nativeDrawFrame(JNIEnv*, jclass,
                                                           jlong nowNs) {
    {
        std::lock_guard<std::mutex> lock(gMutex);
        const int ticks = gClock.Advance(static_cast<int64_t>(nowNs));
        if (ticks > 0) updateDiagnosticCamera(ticks);
    }
    glClearColor(0.055f, 0.065f, 0.085f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!gProgram || gPos < 0 || gUV < 0 || gSampler < 0) return;
    uploadTexture();
    if (!gTexture) return;
    const float scale = std::min(static_cast<float>(gWidth) / wl4::VideoState::Width,
                                 static_cast<float>(gHeight) / wl4::VideoState::Height);
    const float x = scale * wl4::VideoState::Width / gWidth;
    const float y = scale * wl4::VideoState::Height / gHeight;
    const GLfloat quad[] = {
        -x, y, 0.f, 0.f, -x,-y, 0.f,1.f,  x, y, 1.f,0.f,
         x, y, 1.f,0.f, -x,-y, 0.f,1.f,  x,-y, 1.f,1.f
    };
    glUseProgram(gProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gTexture);
    glUniform1i(gSampler, 0);
    glVertexAttribPointer(static_cast<GLuint>(gPos), 2, GL_FLOAT, GL_FALSE,
                          sizeof(GLfloat) * 4, quad);
    glEnableVertexAttribArray(static_cast<GLuint>(gPos));
    glVertexAttribPointer(static_cast<GLuint>(gUV), 2, GL_FLOAT, GL_FALSE,
                          sizeof(GLfloat) * 4, quad + 2);
    glEnableVertexAttribArray(static_cast<GLuint>(gUV));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(static_cast<GLuint>(gPos));
    glDisableVertexAttribArray(static_cast<GLuint>(gUV));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_warioland4_MainActivity_nativeSetButtons(JNIEnv*, jclass,
                                                            jint buttons) {
    std::lock_guard<std::mutex> lock(gMutex);
    gMemory.SetButtons(static_cast<uint16_t>(buttons));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ylports_warioland4_MainActivity_nativeSetSavePath(JNIEnv* env, jclass,
                                                              jstring path) {
    if (!path) return JNI_FALSE;
    const char* name = env->GetStringUTFChars(path, nullptr);
    if (!name) return JNI_FALSE;
    const std::string newPath(name);
    env->ReleaseStringUTFChars(path, name);
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gMemory.LoadSaveFile(newPath)) return JNI_FALSE;
    gSavePath = newPath;
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ylports_warioland4_MainActivity_nativeFlushSave(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(gMutex);
    return !gSavePath.empty() && gMemory.FlushSaveFile(gSavePath);
}
