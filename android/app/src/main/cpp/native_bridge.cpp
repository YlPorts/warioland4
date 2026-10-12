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
#include "room_assets.h"
#include <string>

extern "C" {
#include "fixed_point.h"
#include "wl4_host_game.h"
}

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "wl4-native", __VA_ARGS__)

namespace {
std::mutex gMutex;
wl4::NativeMemory gMemory;
std::string gSavePath;
wl4::VideoState gState;
wl4::GbaVideo gVideo;
wl4::RoomAssets gRoom;
wl4::FrameClock gClock;
std::vector<uint32_t> gPixels;
bool gSceneLoaded = false;
bool gUploadPending = false;
int gCameraBaseX = 0;
int gCameraBaseY = 0;

GLuint gTexture = 0;
GLuint gProgram = 0;
GLint gPos = -1, gUV = -1, gSampler = -1;
int gWidth = 1, gHeight = 1;

// Native room preview. Gameplay logic is not connected yet.
// Bridge the *original* InitializeVideoMemory() function to host RAM.
int hostDmaFill(void* context, uint32_t address, uint32_t pattern,
                uint32_t bytes, uint32_t unitBytes) {
    if (!context) return 0;
    return static_cast<wl4::NativeMemory*>(context)->FillRegion(
        address, pattern, bytes, unitBytes) ? 1 : 0;
}

bool buildRoomScene() {
    std::string error;
    if (!gRoom.Load(gMemory, 0, 0, &error)) {
        LOGE("First room load failed: %s", error.c_str());
        return false;
    }
    wl4HostSetDmaFillHandler(&gMemory, hostDmaFill);
    InitializeVideoMemory();
    wl4::VideoState state;
    gCameraBaseX = gRoom.InitialCameraX();
    gCameraBaseY = gRoom.InitialCameraY();
    gRoom.BuildVideo(gCameraBaseX, gCameraBaseY, &state);
    gScreenShakeX = {};
    gScreenShakeY = {};
    gButtonsHeld = gButtonsHeldCopy = gButtonsPressed = 0;
    gMainGameMode = GM_CUTSCENE;
    gDisableSoftReset = 0;
    gInterruptCheck = 0;
    wl4HostIrqFlags = 0;
    InterruptCallbackSetVBlank(nullptr);
    gMemory.InstallVideoState(state);
    gState = gMemory.CaptureVideo();
    gVideo.Render(gState, &gPixels);
    gSceneLoaded = true;
    gUploadPending = true;
    gClock.Reset();
    return true;
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

void updateRoomPreview(int ticks) {
    if (!gSceneLoaded) return;
    bool redraw = false;
    // Call the original decompiled input and interrupt handlers once
    // per logical frame instead of tying game ticks to the Android refresh rate.
    for (int frame = 0; frame < ticks; ++frame) {
        wl4HostKeyInput = gMemory.Read16(0x04000130);
        PollInput();
        CheckSoftReset(); // original src/main.c function
        if (gMainGameMode == GM_SOFT_RESET) {
            gMainGameMode = GM_CUTSCENE;
            gCameraBaseX = gRoom.InitialCameraX();
            gCameraBaseY = gRoom.InitialCameraY();
            redraw = true;
        }
        if (gButtonsPressed & (256u | 512u)) {
            const int direction = (gButtonsPressed & 256u) ? 1 : -1;
            const int next = (gRoom.RoomId() + direction + gRoom.RoomCount()) % gRoom.RoomCount();
            std::string error;
            if (gRoom.Load(gMemory, gRoom.StageId(), static_cast<uint8_t>(next), &error)) {
                gCameraBaseX = gRoom.InitialCameraX();
                gCameraBaseY = gRoom.InitialCameraY();
                redraw = true;
            } else LOGE("Room load failed: %s", error.c_str());
        }
        const int dx = ((gButtonsHeld & 16u) != 0) - ((gButtonsHeld & 32u) != 0);
        const int dy = ((gButtonsHeld & 128u) != 0) - ((gButtonsHeld & 64u) != 0);
        const int x = std::clamp(gCameraBaseX + dx * 2, 0, std::max(0, gRoom.PixelWidth() - 240));
        const int y = std::clamp(gCameraBaseY + dy * 2, 0, std::max(0, gRoom.PixelHeight() - 160));
        if (x != gCameraBaseX || y != gCameraBaseY) {
            gCameraBaseX = x; gCameraBaseY = y;
            redraw = true;
        }
        gInterruptCheck = 0;
        InterruptCallbackCallVBlank();
    }
    if (!redraw) return;
    gRoom.BuildVideo(gCameraBaseX, gCameraBaseY, &gState);
    gMemory.InstallVideoState(gState);
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
        length < static_cast<std::streamoff>(0x78f2e4)) {
        return JNI_FALSE;
    }
    in.seekg(0);
    std::vector<uint8_t> rom(static_cast<size_t>(length));
    if (!in.read(reinterpret_cast<char*>(rom.data()), length)) return JNI_FALSE;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (!gMemory.LoadRom(std::move(rom))) return JNI_FALSE;
        if (!buildRoomScene()) return JNI_FALSE;
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
        if (ticks > 0) updateRoomPreview(ticks);
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
