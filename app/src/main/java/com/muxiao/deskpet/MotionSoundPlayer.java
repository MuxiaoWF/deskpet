package com.muxiao.deskpet;

import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.File;

/**
 * Plays motion sound files (wav/mp3/ogg) associated with Live2D motions.
 * Only one sound plays at a time; starting a new sound releases the previous one.
 * All MediaPlayer operations are dispatched to the main thread to avoid
 * threading issues when called from the GL thread via JNI.
 */
public class MotionSoundPlayer {

    private static final String TAG = "MotionSoundPlayer";

    private static final AudioAttributes AUDIO_ATTRIBUTES = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build();

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private MediaPlayer currentPlayer;
    private String modelDir;

    public void setModelDir(String dir) {
        this.modelDir = dir;
    }

    /**
     * Play a sound file relative to the model directory.
     *
     * @param soundPath path relative to model dir
     */
    public void play(String soundPath) {
        play(soundPath, 0);
    }

    /**
     * Play a sound file relative to the model directory, with optional delay.
     * Safe to call from any thread (e.g. GL thread via JNI callback).
     *
     * @param soundPath path relative to model dir
     * @param delayMs   delay before playing in milliseconds (0 = immediate)
     */
    public void play(String soundPath, int delayMs) {
        Log.d(TAG, "play: soundPath=[" + soundPath + "] delayMs=" + delayMs + " modelDir=" + modelDir);
        if (modelDir == null || soundPath == null || soundPath.isEmpty()) {
            Log.w(TAG, "play: skipped — modelDir=" + modelDir + " soundPath=" + soundPath);
            return;
        }

        File soundFile = new File(modelDir, soundPath);
        if (!soundFile.exists()) {
            Log.w(TAG, "Sound file not found: " + soundFile.getAbsolutePath());
            return;
        }
        Log.d(TAG, "play: file exists, starting playback: " + soundFile.getAbsolutePath());

        Runnable playTask = () -> {
            releaseInternal();
            try {
                currentPlayer = new MediaPlayer();
                currentPlayer.setAudioAttributes(AUDIO_ATTRIBUTES);
                currentPlayer.setDataSource(soundFile.getAbsolutePath());
                currentPlayer.setOnPreparedListener(mp -> mp.start());
                currentPlayer.setOnCompletionListener(mp -> mp.release());
                currentPlayer.setOnErrorListener((mp, what, extra) -> {
                    Log.w(TAG, "MediaPlayer error: what=" + what + " extra=" + extra);
                    mp.release();
                    return true;
                });
                currentPlayer.prepareAsync();
            } catch (Exception e) {
                Log.w(TAG, "Failed to play sound: " + soundPath, e);
                releaseInternal();
            }
        };

        if (delayMs > 0) {
            mainHandler.postDelayed(playTask, delayMs);
        } else {
            mainHandler.post(playTask);
        }
    }

    public void release() {
        mainHandler.post(this::releaseInternal);
    }

    /** Must be called on the main thread. */
    private void releaseInternal() {
        if (currentPlayer != null) {
            try {
                if (currentPlayer.isPlaying()) {
                    currentPlayer.stop();
                }
                currentPlayer.release();
            } catch (Exception ignored) {}
            currentPlayer = null;
        }
    }
}
