package com.ylports.warioland4;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.Gravity;
import android.view.Choreographer;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.Locale;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Native playable alpha with a portable subset of normal Wario movement.
 * The ROM picker validates stock ROMs or the user's known modified ROM;
 * the native renderer
 * loads original rooms and animated Wario artwork from that ROM.
 */
public final class MainActivity extends Activity {
    private static final int PICK_ROM = 10;
    private static final String US_MD5 = "5fe47355a33e3fabec2a1607af88a404";
    private static final String USER_MD5 = "22b8707268a6c9c15cb9b8d2ec09754d";
    private static final String JP_MD5 = "99c8ad779a16be513a9fdff502b6f5c2";
    private GLSurfaceView surface;
    private TextView status;
    private boolean pickerOpen;
    private boolean rendering;
    private final AudioPump audio = new AudioPump();
    private boolean romLoaded;
    private long nextStatus;
    private final Choreographer.FrameCallback frameCallback = new Choreographer.FrameCallback() {
        @Override public void doFrame(long frameTimeNanos) {
            if (!rendering || surface == null) return;
            surface.requestRender();
            if (romLoaded && frameTimeNanos >= nextStatus) {
                status.setText(nativeStatus());
                nextStatus = frameTimeNanos + 500000000L;
            }
            Choreographer.getInstance().postFrameCallback(this);
        }
    };

    static {
        System.loadLibrary("wl4_native");
    }

    static native boolean nativeLoadRom(String filePath);
    static native void nativeSurfaceCreated();
    static native void nativeSurfaceChanged(int width, int height);
    static native void nativeDrawFrame(long monotonicNanos);
    static native void nativeResetClock();
    static native boolean nativeSetSavePath(String path);
    static native boolean nativeFlushSave();
    static native void nativeSetButtons(int buttons);
    static native void nativeRenderAudio(short[] stereo);
    static native String nativeStatus();

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN);

        FrameLayout root = new FrameLayout(this);
        surface = new GLSurfaceView(this);
        surface.setEGLContextClientVersion(2);
        surface.setRenderer(new GLSurfaceView.Renderer() {
            @Override public void onSurfaceCreated(GL10 unused, EGLConfig config) {
                nativeSurfaceCreated();
            }
            @Override public void onSurfaceChanged(GL10 unused, int width, int height) {
                nativeSurfaceChanged(width, height);
            }
            @Override public void onDrawFrame(GL10 unused) {
                nativeDrawFrame(System.nanoTime());
            }
        });
        surface.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        root.addView(surface);

        ControlsView controls = new ControlsView(this, MainActivity::nativeSetButtons);
        root.addView(controls, new FrameLayout.LayoutParams(-1, -1));

        status = new TextView(this);
        status.setTextColor(0xffffffff);
        status.setBackgroundColor(0x88000000);
        status.setTextSize(13);
        status.setPadding(14, 8, 14, 8);
        status.setText("Wario Land 4 • Alfa jugable del primer pasaje");
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(-2, -2);
        params.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        root.addView(status, params);
        setContentView(root);
        nativeSetSavePath(new File(getFilesDir(), "wl4-native.sav").getAbsolutePath());

        // Tap banner to re-import a ROM. On first run the picker opens once.
        status.setOnClickListener(v -> pickRom());
        File existing = new File(getFilesDir(), "baserom.gba");
        if (existing.exists()) {
            if (!loadAndValidate(existing)) {
                existing.delete();
                pickRom();
            }
        } else {
            pickRom();
        }
    }

    private void pickRom() {
        if (pickerOpen) return;
        pickerOpen = true;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_ROM);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM) return;
        pickerOpen = false;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            status.setText("ROM no seleccionada • Toca aquí para elegir un archivo .gba");
            return;
        }

        Uri uri = data.getData();
        File temporary = new File(getFilesDir(), "baserom.gba.tmp");
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(temporary)) {
            if (in == null) throw new IllegalArgumentException("Archivo no disponible");
            byte[] buffer = new byte[64 * 1024];
            int n;
            long size = 0;
            while ((n = in.read(buffer)) != -1) {
                size += n;
                if (size > 32L * 1024 * 1024) {
                    throw new IllegalArgumentException("ROM demasiado grande");
                }
                out.write(buffer, 0, n);
            }
        } catch (Exception e) {
            temporary.delete();
            status.setText("No se pudo leer la ROM: " + e.getMessage());
            return;
        }

        String version = checkRom(temporary);
        if (version == null) {
            temporary.delete();
            status.setText("ROM incompatible. Usa Wario Land 4 USA/EU, JP o tu ROM personalizada verificada.");
            return;
        }
        File installed = new File(getFilesDir(), "baserom.gba");
        if (installed.exists() && !installed.delete()) {
            temporary.delete();
            status.setText("No se pudo reemplazar la ROM existente");
            return;
        }
        if (!temporary.renameTo(installed)) {
            temporary.delete();
            status.setText("No se pudo guardar la ROM");
            return;
        }
        loadAndValidate(installed);
    }

    private boolean loadAndValidate(File file) {
        romLoaded = false;
        String version = checkRom(file);
        if (version == null || !nativeLoadRom(file.getAbsolutePath())) {
            status.setText("Error al validar o decodificar recursos de ROM");
            return false;
        }
        romLoaded = true;
        status.setText(nativeStatus());
        return true;
    }

    private String checkRom(File file) {
        try (FileInputStream input = new FileInputStream(file)) {
            MessageDigest md5 = MessageDigest.getInstance("MD5");
            byte[] data = new byte[64 * 1024];
            int n;
            while ((n = input.read(data)) != -1) md5.update(data, 0, n);
            StringBuilder digest = new StringBuilder();
            for (byte b : md5.digest()) digest.append(String.format(Locale.US, "%02x", b & 0xff));
            if (US_MD5.equals(digest.toString())) return "USA/EU";
            if (JP_MD5.equals(digest.toString())) return "JP";
            if (USER_MD5.equals(digest.toString())) return "USA personalizada";
        } catch (Exception ignored) {}
        return null;
    }

    @Override protected void onPause() {
        rendering = false;
        audio.stop();
        Choreographer.getInstance().removeFrameCallback(frameCallback);
        nativeSetButtons(0);
        nativeFlushSave();
        surface.onPause();
        super.onPause();
    }

    @Override protected void onResume() {
        super.onResume();
        if (surface != null) {
            surface.onResume();
            nativeResetClock();
            audio.start();
            rendering = true;
            Choreographer.getInstance().postFrameCallback(frameCallback);
        }
    }
}
