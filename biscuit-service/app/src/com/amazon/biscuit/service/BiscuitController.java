/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.service;

import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.content.Intent;
import android.media.AudioManager;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiManager;
import android.util.Log;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;

/** Device actions shared by the command receiver, the AIDL service and the app. */
final class BiscuitController {
    private static final String TAG = "BiscuitService";

    static final String PKG = "com.amazon.biscuit.service";
    static final String BLUETOOTH_PAIRING_MODE = PKG + ".BLUETOOTH_PAIRING_MODE";
    static final String BLUETOOTH_OFF = PKG + ".BLUETOOTH_OFF";
    static final String WIFI_ON = PKG + ".WIFI_ON";
    static final String WIFI_OFF = PKG + ".WIFI_OFF";
    static final String WIFI_CONNECT = PKG + ".WIFI_CONNECT";
    static final String VOLUME_UP = PKG + ".VOLUME_UP";
    static final String VOLUME_DOWN = PKG + ".VOLUME_DOWN";
    static final String VOLUME_SET = PKG + ".VOLUME_SET";
    static final String MIC_MUTE_ON = PKG + ".MICROPHONE_MUTE_ON";
    static final String MIC_MUTE_OFF = PKG + ".MICROPHONE_MUTE_OFF";
    static final String MIC_MUTE_TOGGLE = PKG + ".MICROPHONE_MUTE_TOGGLE";
    static final String COUNTDOWN_PROGRESS = PKG + ".COUNTDOWN_PROGRESS";
    static final String COUNTDOWN_CLEAR = PKG + ".COUNTDOWN_CLEAR";

    private static final String EXTRA_SSID = "ssid";
    private static final String EXTRA_PSK = "psk";
    private static final String EXTRA_VOLUME = "volume";
    /* Set by PhoneWindowManager on a real button press. */
    private static final String EXTRA_FROM_BUTTON = PKG + ".EXTRA_FROM_BUTTON";
    private static final String EXTRA_COUNTDOWN_REMAINING_MS = PKG + ".EXTRA_COUNTDOWN_REMAINING_MS";
    private static final String EXTRA_COUNTDOWN_TOTAL_MS = PKG + ".EXTRA_COUNTDOWN_TOTAL_MS";

    private static final String SOCKET = "biscuit-ledd";
    /* amz_priv: the kernel owns microphone privacy (init.target.biscuit.rc). */
    private static final String PRIVACY_STATE = "/dev/biscuit_privacy/privacy_state";
    /* setScanMode() takes milliseconds since Android 11. */
    private static final long PAIRING_MODE_MILLIS = 300_000L;

    private static final Object sLock = new Object();

    private final Context mContext;

    BiscuitController(Context context) {
        mContext = context.getApplicationContext();
    }

    /** Runs one CLI/key action. Returns false for unknown actions. */
    boolean handle(Intent intent) {
        String action = intent != null ? intent.getAction() : null;
        if (action == null) return false;
        switch (action) {
            case BLUETOOTH_PAIRING_MODE: startBluetoothPairingMode(); break;
            case BLUETOOTH_OFF: stopBluetooth(); break;
            case WIFI_ON: setWifiEnabled(true); break;
            case WIFI_OFF: setWifiEnabled(false); break;
            case WIFI_CONNECT:
                connectWifi(intent.getStringExtra(EXTRA_SSID), intent.getStringExtra(EXTRA_PSK));
                break;
            case VOLUME_UP: volumeButton(intent, "up", AudioManager.ADJUST_RAISE); break;
            case VOLUME_DOWN: volumeButton(intent, "down", AudioManager.ADJUST_LOWER); break;
            case VOLUME_SET: setVolume(intent.getIntExtra(EXTRA_VOLUME, -1)); break;
            case COUNTDOWN_PROGRESS:
                setCountdown(intent.getLongExtra(EXTRA_COUNTDOWN_REMAINING_MS, -1),
                        intent.getLongExtra(EXTRA_COUNTDOWN_TOTAL_MS, -1));
                break;
            case COUNTDOWN_CLEAR: send("CLEAR"); break;
            case MIC_MUTE_ON: setMicMuted(true); break;
            case MIC_MUTE_OFF: setMicMuted(false); break;
            case MIC_MUTE_TOGGLE: setMicMuted(!isMicMuted()); break;
            default: return false;
        }
        return true;
    }

    private AudioManager audio() {
        return mContext.getSystemService(AudioManager.class);
    }

    /** Kernel privacy state; falls back to Android's flag without amz_priv. */
    private boolean isMicMuted() {
        try (FileInputStream in = new FileInputStream(PRIVACY_STATE)) {
            int c = in.read();
            if (c == '0' || c == '1') return c == '1';
        } catch (IOException ignored) {
        }
        return audio().isMicrophoneMute();
    }

    /** Muting always works; while the kernel is private, only the button unmutes. */
    void setMicMuted(boolean muted) {
        audio().setMicrophoneMute(muted);
        if (!muted && isMicMuted()) {
            Log.i(TAG, "microphone privacy is on; press the microphone button to unmute");
        }
    }

    void updateVolumeLed(int stream, int current) {
        send("VOLUME " + current + " " + audio().getStreamMaxVolume(stream));
    }

    private void updateVolumeLed() {
        AudioManager audio = audio();
        updateVolumeLed(AudioManager.STREAM_MUSIC, audio.getStreamVolume(AudioManager.STREAM_MUSIC));
    }

    /* A button press goes to the app holding the buttons, if any is left;
     * biscuit_service volume up and down always change the volume. */
    private void volumeButton(Intent intent, String direction, int adjust) {
        if (intent.getBooleanExtra(EXTRA_FROM_BUTTON, false)
                && VolumeButtonClaim.forward(mContext, direction)) {
            return;
        }
        adjustVolume(adjust);
    }

    private void adjustVolume(int direction) {
        audio().adjustStreamVolume(AudioManager.STREAM_MUSIC, direction, 0);
        updateVolumeLed();
    }

    private void setVolume(int volume) {
        AudioManager audio = audio();
        int max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        audio.setStreamVolume(AudioManager.STREAM_MUSIC, Math.max(0, Math.min(volume, max)), 0);
        updateVolumeLed();
    }

    private void setCountdown(long remainingMs, long totalMs) {
        if (totalMs <= 0 || remainingMs < 0 || remainingMs > totalMs) return;
        send(remainingMs == 0 ? "CLEAR" : "COUNTDOWN " + remainingMs + " " + totalMs);
    }

    private void startBluetoothPairingMode() {
        final BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null) return;
        if (!adapter.isEnabled()) adapter.enable();
        new Thread(() -> {
            for (int i = 0; i < 20 && !adapter.isEnabled(); i++) {
                try {
                    Thread.sleep(500);
                } catch (InterruptedException e) {
                    return;
                }
            }
            if (adapter.isEnabled()) {
                adapter.setScanMode(BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE,
                        PAIRING_MODE_MILLIS);
            }
        }, "BiscuitBtPairingMode").start();
    }

    private void stopBluetooth() {
        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter != null && adapter.isEnabled()) adapter.disable();
    }

    private WifiManager wifi() {
        return mContext.getSystemService(WifiManager.class);
    }

    private void setWifiEnabled(boolean enabled) {
        WifiManager wifi = wifi();
        if (wifi == null) return;
        wifi.setWifiEnabled(enabled);
        if (enabled) wifi.reconnect();
    }

    private static int findNetwork(WifiManager wifi, String ssid) {
        List<WifiConfiguration> configs = wifi.getConfiguredNetworks();
        if (configs == null) return -1;
        for (WifiConfiguration config : configs) {
            if (config != null && ssid.equals(config.SSID)) return config.networkId;
        }
        return -1;
    }

    private void connectWifi(String ssid, String psk) {
        if (ssid == null || ssid.isEmpty()) return;
        WifiManager wifi = wifi();
        if (wifi == null) return;

        WifiConfiguration config = new WifiConfiguration();
        config.SSID = quote(ssid);
        if (psk == null || psk.isEmpty()) {
            config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
        } else {
            config.preSharedKey = isHexPsk(psk) ? psk : quote(psk);
            config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.WPA_PSK);
        }
        wifi.setWifiEnabled(true);
        int id = findNetwork(wifi, config.SSID);
        if (id >= 0) {
            config.networkId = id;
            id = wifi.updateNetwork(config);
        } else {
            id = wifi.addNetwork(config);
        }
        if (id >= 0) {
            wifi.enableNetwork(id, true);
            wifi.reconnect();
        } else {
            Log.w(TAG, "could not save the Wi-Fi network");
        }
    }

    private static String quote(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    private static boolean isHexPsk(String s) {
        if (s.length() != 64) return false;
        for (int i = 0; i < s.length(); i++) {
            if (Character.digit(s.charAt(i), 16) < 0) return false;
        }
        return true;
    }

    /** Sends one command to biscuit-ledd; returns its reply or null. */
    String send(String command) {
        synchronized (sLock) {
            LocalSocket socket = new LocalSocket();
            try {
                socket.connect(new LocalSocketAddress(SOCKET, LocalSocketAddress.Namespace.RESERVED));
                OutputStream out = socket.getOutputStream();
                out.write((command + "\n").getBytes(StandardCharsets.US_ASCII));
                out.flush();
                BufferedReader in = new BufferedReader(
                        new InputStreamReader(socket.getInputStream(), StandardCharsets.US_ASCII));
                return in.readLine();
            } catch (IOException e) {
                Log.w(TAG, "biscuit-ledd: " + e);
                return null;
            } finally {
                try {
                    socket.close();
                } catch (IOException ignored) {
                }
            }
        }
    }

    boolean sendOk(String command) {
        return "OK".equals(send(command));
    }
}
