/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.service;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.RemoteException;

/** Bound LED ring and microphone interface (IBiscuitService). */
public final class BiscuitService extends Service {
    private BiscuitController mController;

    private final IBiscuitService.Stub mBinder = new IBiscuitService.Stub() {
        @Override
        public boolean play(String name) throws RemoteException {
            checkName(name);
            return mController.sendOk("PLAY " + name);
        }

        @Override
        public boolean setFrame(String rgbHex72) throws RemoteException {
            checkFrame(rgbHex72);
            return mController.sendOk("FRAME " + rgbHex72);
        }

        @Override
        public boolean setVolume(int current, int max) throws RemoteException {
            if (current < 0 || max <= 0 || current > max) throw new RemoteException("bad volume");
            return mController.sendOk("VOLUME " + current + " " + max);
        }

        @Override
        public boolean setMicMuted(boolean muted) {
            mController.setMicMuted(muted);
            return true;
        }

        @Override
        public boolean clear() {
            return mController.sendOk("OFF");
        }

        @Override
        public String status() throws RemoteException {
            String reply = mController.send("STATUS");
            if (reply == null) throw new RemoteException("no daemon reply");
            return reply;
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        mController = new BiscuitController(this);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mBinder;
    }

    private static void checkName(String name) throws RemoteException {
        if (name == null || name.length() < 1 || name.length() > 64 || name.contains("..")) {
            throw new RemoteException("bad name");
        }
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            if (!ok) throw new RemoteException("bad name");
        }
    }

    private static void checkFrame(String frame) throws RemoteException {
        if (frame == null || frame.length() != 72) throw new RemoteException("bad frame");
        for (int i = 0; i < frame.length(); i++) {
            if (Character.digit(frame.charAt(i), 16) < 0) throw new RemoteException("bad frame");
        }
    }
}
