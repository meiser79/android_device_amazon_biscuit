/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.service;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/** Explicit broadcasts from /system/bin/biscuit_service and biscuit-keyd. */
public final class CommandReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        final PendingResult result = goAsync();
        new Thread(() -> {
            try {
                if (!new BiscuitController(context).handle(intent)) {
                    Log.w("BiscuitService", "unknown action " + intent.getAction());
                }
            } finally {
                result.finish();
            }
        }, "BiscuitCommand").start();
    }
}
