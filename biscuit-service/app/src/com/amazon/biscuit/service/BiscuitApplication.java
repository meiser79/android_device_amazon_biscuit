/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.service;

import android.app.Application;
import android.bluetooth.BluetoothA2dpSink;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioManager;
import android.util.Log;

/**
 * Persistent headless companion: mirrors volume on the LED ring and makes
 * Biscuit behave as a Bluetooth speaker without any pairing UI.
 */
public final class BiscuitApplication extends Application {
    private static final String TAG = "BiscuitService";
    private static final String CLASSIC_SPEAKER_PIN = "0000";

    private BiscuitController mController;
    private BluetoothA2dpSink mA2dpSink;

    private final BroadcastReceiver mVolumeReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int stream = intent.getIntExtra(AudioManager.EXTRA_VOLUME_STREAM_TYPE, -1);
            if (stream != AudioManager.STREAM_MUSIC) return;
            mController.updateVolumeLed(stream,
                    intent.getIntExtra(AudioManager.EXTRA_VOLUME_STREAM_VALUE, 0));
        }
    };

    /* Ordered broadcast: answer before Settings would show a dialog. */
    private final BroadcastReceiver mPairingReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
            int variant = intent.getIntExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT,
                    BluetoothDevice.ERROR);
            if (device == null) return;
            switch (variant) {
                case BluetoothDevice.PAIRING_VARIANT_PIN:
                    device.setPin(BluetoothDevice.convertPinToBytes(CLASSIC_SPEAKER_PIN));
                    break;
                case BluetoothDevice.PAIRING_VARIANT_PASSKEY_CONFIRMATION:
                case BluetoothDevice.PAIRING_VARIANT_CONSENT:
                    device.setPairingConfirmation(true);
                    break;
                default:
                    Log.w(TAG, "unsupported pairing variant " + variant);
                    return;
            }
            if (isOrderedBroadcast()) abortBroadcast();
        }
    };

    private final BroadcastReceiver mBondReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR)
                    != BluetoothDevice.BOND_BONDED) {
                return;
            }
            BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
            BluetoothA2dpSink sink = mA2dpSink;
            if (device == null || sink == null) return;
            sink.setConnectionPolicy(device, BluetoothProfile.CONNECTION_POLICY_ALLOWED);
            sink.connect(device);
        }
    };

    private final BluetoothProfile.ServiceListener mProfileListener =
            new BluetoothProfile.ServiceListener() {
                @Override
                public void onServiceConnected(int profile, BluetoothProfile proxy) {
                    if (profile == BluetoothProfile.A2DP_SINK) mA2dpSink = (BluetoothA2dpSink) proxy;
                }

                @Override
                public void onServiceDisconnected(int profile) {
                    if (profile == BluetoothProfile.A2DP_SINK) mA2dpSink = null;
                }
            };

    @Override
    public void onCreate() {
        super.onCreate();
        mController = new BiscuitController(this);

        registerReceiver(mVolumeReceiver, new IntentFilter(AudioManager.VOLUME_CHANGED_ACTION));

        IntentFilter pairing = new IntentFilter(BluetoothDevice.ACTION_PAIRING_REQUEST);
        pairing.setPriority(IntentFilter.SYSTEM_HIGH_PRIORITY);
        registerReceiver(mPairingReceiver, pairing);
        registerReceiver(mBondReceiver, new IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED));

        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter != null) {
            adapter.getProfileProxy(this, mProfileListener, BluetoothProfile.A2DP_SINK);
        }
    }
}
