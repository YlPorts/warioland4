#include <GLES2/gl2.h>
#include <jni.h>
#include <android/log.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>
#include <algorithm>
#include "rom_assets.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "wl4-native", __VA_ARGS__)

namespace {
constexpr size_t kPaletteOffset = 0x400A68; // common_sprites.pal (US/JP)
constexpr size_t kTileOffset = 0x400AE8;    // common_sprites.4bpp (US/JP)
constexpr size_t kTileCount = 0x3000 / 32;
std::mutex gMutex;
std::vector<uint8_t> gPixels;
int gImageWidth = 0, gImageHeight = 0;
bool gUploadPending = false;
std::atomic<int> gButtons{0};
GLuint gTexture = 0;
GLuint gProgram = 0;
GLint gPos = -1, gUV = -1, gSampler = -1;
int gWidth = 1, gHeight = 1;

GLuint shader(GLenum type, const char* code) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &code, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char msg[512] = {};
        glGetShaderInfoLog(s, sizeof(msg), nullptr, msg);
        LOGE("GL shader compile failed: %s", msg);
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gImageWidth, gImageHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, gPixels.data());
    gUploadPending = false;
}
}

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
        length < static_cast<std::streamoff>(kTileOffset + kTileCount * 32)) {
        return JNI_FALSE;
    }
    in.seekg(0);
    std::vector<uint8_t> rom(static_cast<size_t>(length));
    if (!in.read(reinterpret_cast<char*>(rom.data()), length)) return JNI_FALSE;

    std::vector<uint8_t> pixels;
    int width = 0, height = 0;
    if (!wl4::Decode4bppTiles(rom.data(), rom.size(),
                             kPaletteOffset, kTileOffset, kTileCount,
                             16, &pixels, &width, &height)) return JNI_FALSE;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gPixels = std::move(pixels);
        gImageWidth = width;
        gImageHeight = height;
        gUploadPending = true;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ylports_warioland4_MainActivity_nativeSurfaceCreated(JNIEnv*, jclass) {
    // A recreated EGL context has invalidated the previous GL objects.
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
Java_com_ylports_warioland4_MainActivity_nativeDrawFrame(JNIEnv*, jclass) {
    glClearColor(0.055f, 0.065f, 0.085f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!gProgram) return;
    uploadTexture();
    if (!gTexture) return;
    int iw, ih;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        iw = gImageWidth;
        ih = gImageHeight;
    }
    if (iw <= 0 || ih <= 0) return;
    const float scale = std::min(static_cast<float>(gWidth) / iw,
                                 static_cast<float>(gHeight) / ih);
    const float x = scale * iw / gWidth;
    const float y = scale * ih / gHeight;
    const GLfloat quad[] = {
        -x,  y, 0.f, 0.f,  -x, -y, 0.f, 1.f,   x,  y, 1.f, 0.f,
         x,  y, 1.f, 0.f,  -x, -y, 0.f, 1.f,   x, -y, 1.f, 1.f
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
Java_com_ylports_warioland4_MainActivity_nativeSetButtons(JNIEnv*, jclass, jint buttons) {
    gButtons.store(buttons, std::memory_order_relaxed);
}
