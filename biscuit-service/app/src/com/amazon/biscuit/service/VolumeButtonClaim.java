/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.service;

import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.os.RemoteException;
import android.os.SystemProperties;

/**
 * The app holding the volume buttons (IBiscuitService.claimVolumeButtons()),
 * as on the Kiosk Satellite Edition image. While it holds them, presses go to
 * it as VOLUME_BUTTON broadcasts instead of changing the volume; the claim
 * ends with its token, so a crashed app never leaves the buttons dead.
 */
final class VolumeButtonClaim {
    static final String VOLUME_BUTTON = BiscuitController.PKG + ".VOLUME_BUTTON";
    private static final String EXTRA_DIRECTION = "direction";
    /* Read by PhoneWindowManager (patches/frameworks/base): while it is 1,
     * the volume keys come to CommandReceiver instead of changing the volume. */
    private static final String CLAIMED_PROP = "sys.biscuit.volume_claimed";

    private static final Object sLock = new Object();
    private static IBinder sToken;
    private static IBinder.DeathRecipient sDeath;
    private static String sPackage;

    private VolumeButtonClaim() {
    }

    /** After a restart of this process no claim is left. */
    static void reset() {
        synchronized (sLock) {
            releaseLocked();
        }
    }

    /** Claims or gives back the buttons for {@code pkg}, as Kiosk Satellite expects. */
    static boolean set(IBinder token, String pkg, boolean claimed) throws RemoteException {
        synchronized (sLock) {
            if (!claimed) {
                if (token != sToken) return false;
                releaseLocked();
                return true;
            }
            if (token == sToken) return true;
            releaseLocked();
            IBinder.DeathRecipient death = () -> {
                synchronized (sLock) {
                    if (sToken == token) releaseLocked();
                }
            };
            token.linkToDeath(death, 0);
            sToken = token;
            sDeath = death;
            sPackage = pkg;
            SystemProperties.set(CLAIMED_PROP, "1");
            return true;
        }
    }

    /** Sends a press to the claiming app. False when nothing holds the buttons. */
    static boolean forward(Context context, String direction) {
        String target;
        synchronized (sLock) {
            target = sPackage;
        }
        if (target == null) return false;
        Intent press = new Intent(VOLUME_BUTTON);
        press.setPackage(target);
        press.addFlags(Intent.FLAG_RECEIVER_FOREGROUND | Intent.FLAG_RECEIVER_REGISTERED_ONLY);
        press.putExtra(EXTRA_DIRECTION, direction);
        context.sendBroadcast(press);
        return true;
    }

    private static void releaseLocked() {
        if (sToken != null) {
            try {
                sToken.unlinkToDeath(sDeath, 0);
            } catch (RuntimeException ignored) {
            }
        }
        sToken = null;
        sDeath = null;
        sPackage = null;
        SystemProperties.set(CLAIMED_PROP, "0");
    }
}
