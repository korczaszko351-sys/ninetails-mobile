package com.aerophone.aeroradar.service

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

/**
 * BootReceiver — restartuje radar po restarcie systemu.
 *
 * Jako priv-app z RECEIVE_BOOT_COMPLETED może wystartować usługę
 * bez interakcji użytkownika. Uruchamiamy tylko jeśli użytkownik
 * włączył radar w ustawieniach (flaga w SharedPreferences).
 */
class BootReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action ?: return
        if (action != Intent.ACTION_BOOT_COMPLETED &&
            action != Intent.ACTION_LOCKED_BOOT_COMPLETED) return

        val prefs = context.getSharedPreferences("aeroradar", Context.MODE_PRIVATE)
        val enabled = prefs.getBoolean("radar_enabled", false)
        if (!enabled) {
            Log.i("AeroRadar/Boot", "radar wyłączony w ustawieniach — nie startuję")
            return
        }

        Log.i("AeroRadar/Boot", "restart radaru po boocie")
        RadarService.start(context)
    }
}
