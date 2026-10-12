package com.ylports.warioland4;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;

/** Streams native PCM on its own worker; no audio writes on the UI/GL thread. */
final class AudioPump {
    private volatile boolean running;
    private Thread thread;

    void start() {
        if (thread != null) return;
        running = true;
        thread = new Thread(() -> {
            AudioTrack output = null;
            try {
                int minimum = AudioTrack.getMinBufferSize(44100,
                        AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
                output = new AudioTrack.Builder()
                        .setAudioAttributes(new AudioAttributes.Builder()
                                .setUsage(AudioAttributes.USAGE_GAME)
                                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                        .setAudioFormat(new AudioFormat.Builder().setSampleRate(44100)
                                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                                .setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                        .setBufferSizeInBytes(Math.max(8192, minimum))
                        .setTransferMode(AudioTrack.MODE_STREAM).build();
                if (output.getState() != AudioTrack.STATE_INITIALIZED) return;
                short[] pcm = new short[2048];
                output.play();
                while (running) {
                    MainActivity.nativeRenderAudio(pcm);
                    int cursor = 0;
                    while (running && cursor < pcm.length) {
                        int written = output.write(pcm, cursor, pcm.length-cursor,
                                AudioTrack.WRITE_BLOCKING);
                        if (written <= 0) return;
                        cursor += written;
                    }
                }
            } catch (IllegalArgumentException | IllegalStateException ignored) {
                // A device audio failure must not prevent playing or saving.
            } finally {
                if (output != null) {
                    try { output.stop(); } catch (IllegalStateException ignored) {}
                    output.release();
                }
            }
        }, "wl4-native-audio");
        thread.start();
    }

    void stop() {
        running = false;
        if (thread != null) {
            try { thread.join(500); } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            // WRITE_BLOCKING is bounded by this small stream buffer.
            if (!thread.isAlive()) thread = null;
        }
    }
}
